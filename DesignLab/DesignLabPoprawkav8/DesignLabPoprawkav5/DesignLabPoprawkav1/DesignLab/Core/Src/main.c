/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32f3xx_hal.h"
#include "4b_HD44780_LCD.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
	#define CMD_BUFFER_SIZE 			20
	#define CAPACITY					19.5f 				//declared 25F
	#define CHARGE_CURRENT				0.210f
	#define CHARGE_TIME_MARGIN 			1.15F 				//calibration constant
	#define V_MIN_MV 					2900.0f
	#define V_MAX_MV					3200.0f
	#define MIN_ENERGY					(0.5 * CAPACITY * V_MIN_MV/1000 * V_MIN_MV/1000)
	#define MAX_ENERGY					(0.5 * CAPACITY * V_MAX_MV/1000 * V_MAX_MV/1000)
	#define LCD_UPDATE_INTERVAL_MS		500
	#define UART_UPDATE_INTERVAL_MS		10
	#define UART_TIMEOUT_MS				100
	#define PWM_INITIAL_VALUE			100
	#define PWM_MAX_VALUE				1000
	#define CHARGE_FULL_MAGIC			999
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
	char 				txbufUARTcapacitor[25],
		 	 	 	 	txbufUARTresistor[25],
						txbufLCDvoltage[30],
						txbufLCDpercent[25];

	char 				RxBuffer[CMD_BUFFER_SIZE];

	uint32_t 			last_lcd_update = 0,
			 	 	 	last_uart_resistor_update = 0,
			 	 	 	last_uart_capacitor_update = 0,
						target_charging_time_units = 0;

	const uint16_t 		ADC_MAX = 4095,
						VREF_ADC = 3361;

	uint8_t 			RxData[1];

	volatile float		current_voltage_cap_mV = 0,
						current_voltage_res_mV = 0,
						current_energy = 0,
						start_state_energy = 0,
						current_percent = 0;

	volatile uint8_t 	RxIndex = 0;

	volatile uint32_t  	ms_elapsed_charging = 0,
						s_elapsed_running = 0,
						start_state = 100;


	volatile char  		adc1Resistor_convert_flag,
				   	   	adc2Capacitor_convert_flag;

	volatile uint8_t 	cmd_ready_flag = 0,
						charging_active_flag = 0,
						discharging_active_flag = 0,
						pwm_not_allowed_value_flag = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void ProcessCommand(char *cmd);
float PredictDischargeTimeMs(uint32_t adc_raw);
void StopDischarging(void);
void StartDischarging(void);
void Charge(int);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_ADC2_Init();
  MX_TIM6_Init();
  MX_TIM7_Init();
  MX_TIM15_Init();
  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_IT(&huart2, RxData, 1);
  HAL_TIM_Base_Start(&htim2); //adc2 external trigger
  HAL_TIM_Base_Start(&htim6); //adc1 external trigger
  HAL_ADC_Start_IT(&hadc2);

  // loading the initial pwm value to 50%
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, PWM_INITIAL_VALUE);

  LCD_HD44780_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

	  if (charging_active_flag == 1)
	      {
	          // if the value of target voltage is exceeded, stop charging the capacitor
	          if (ms_elapsed_charging >= target_charging_time_units)
	          {
	              HAL_GPIO_WritePin(GPIOB, BQ25173_CE_Pin, GPIO_PIN_SET);
	              HAL_TIM_Base_Stop_IT(&htim7);
	              charging_active_flag = 0;
	              snprintf(txbufUARTresistor, sizeof(txbufUARTresistor), "log$koniec\r\n");
	              HAL_UART_Transmit(&huart2, (uint8_t*)txbufUARTresistor, strlen(txbufUARTresistor), UART_TIMEOUT_MS);
	          }
	          // we do not check based on voltage, because the charger has a hardware limitation of 3.35 V
	      }

	  if (cmd_ready_flag) {
		  ProcessCommand(RxBuffer);
		  cmd_ready_flag = 0;
		  RxIndex = 0;
	  }

	  if ( uwTick - last_lcd_update >= LCD_UPDATE_INTERVAL_MS && adc2Capacitor_convert_flag) {
			  last_lcd_update = uwTick;
			  adc2Capacitor_convert_flag = 0;

			  uint16_t v_int = (uint16_t)(current_voltage_cap_mV / 1000);
			  uint16_t v_dec = (uint16_t)((int)current_voltage_cap_mV / 10 % 100);
			  uint16_t min = 0, sec = 0;

			  LCD_SetCursor(0, 0);

			 if (discharging_active_flag) {
				 if (current_voltage_cap_mV <= V_MIN_MV) StopDischarging();
				 float predicted_sec = PredictDischargeTimeMs(current_percent);
				 min = (uint16_t)(predicted_sec / 60);
				 sec = (uint16_t)predicted_sec % 60;
				 snprintf(txbufLCDvoltage, sizeof(txbufLCDvoltage), "U = %1u,%02u V %2u:%02u", v_int, v_dec, min, sec);
				 LCD_Print(txbufLCDvoltage);
			 } else {
				 snprintf(txbufLCDvoltage, sizeof(txbufLCDvoltage), "U = %1u,%02u V      ", v_int, v_dec);
				 LCD_Print(txbufLCDvoltage);
			 }

			  LCD_SetCursor(1, 0);

			  snprintf(txbufLCDpercent, sizeof(txbufLCDpercent), "Battery  =  %3u%%", (uint16_t)current_percent);
			  LCD_Print(txbufLCDpercent);
	  }

	  if ( uwTick - last_uart_resistor_update >= UART_UPDATE_INTERVAL_MS && adc1Resistor_convert_flag) {
	  	  last_uart_resistor_update = uwTick;
		  adc1Resistor_convert_flag = 0;

		  uint16_t v_int = (uint16_t)(current_voltage_res_mV / 1000);
		  uint16_t v_dec = (uint16_t)((int)current_voltage_res_mV % 1000);
		  snprintf(txbufUARTresistor, sizeof(txbufUARTresistor), "ADC1$%1u.%03u\r\n", v_int, v_dec);
		  HAL_UART_Transmit(&huart2, (uint8_t*)txbufUARTresistor, strlen(txbufUARTresistor), UART_TIMEOUT_MS);
	  }


	  if ( uwTick - last_uart_capacitor_update >= UART_UPDATE_INTERVAL_MS && adc2Capacitor_convert_flag) {
	  	  last_uart_capacitor_update = uwTick;
		  adc2Capacitor_convert_flag = 0;

		  uint16_t v_int = (uint16_t)(current_voltage_cap_mV / 1000);
		  uint16_t v_dec = (uint16_t)((int)current_voltage_cap_mV % 1000);
		  snprintf(txbufUARTcapacitor, sizeof(txbufUARTcapacitor), "ADC2$%1u.%03u\r\n", v_int, v_dec);
		  HAL_UART_Transmit(&huart2, (uint8_t*)txbufUARTcapacitor, strlen(txbufUARTcapacitor), UART_TIMEOUT_MS);
	  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_TIM1|RCC_PERIPHCLK_ADC12;
  PeriphClkInit.Adc12ClockSelection = RCC_ADC12PLLCLK_DIV1;
  PeriphClkInit.Tim1ClockSelection = RCC_TIM1CLK_HCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
	void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* h) {
		if (h == &hadc1) {
			uint32_t adc1Resistor_result = HAL_ADC_GetValue(&hadc1);
			current_voltage_res_mV = ((float)adc1Resistor_result / ADC_MAX) * VREF_ADC;
			adc1Resistor_convert_flag = 1;
			HAL_ADC_Start_IT(&hadc1);
		}
		else if (h == &hadc2) {
			uint32_t adc2Capacitor_result = HAL_ADC_GetValue(&hadc2);
			current_voltage_cap_mV = ((float)adc2Capacitor_result / ADC_MAX) * VREF_ADC;
			current_energy = 0.5f * CAPACITY * (current_voltage_cap_mV / 1000) * (current_voltage_cap_mV / 1000);
			
			// Calculate percentage with protection against invalid values
			float energy_range = MAX_ENERGY - MIN_ENERGY;
			if (energy_range > 0.01f) {
				current_percent = (current_energy - MIN_ENERGY) * (100.0f / energy_range);
			} else {
				current_percent = 0;
			}

			if (current_percent < 0 || isnan(current_percent)) {
				current_percent = 0;
			} else if (current_percent > 100 || isinf(current_percent)) {
				current_percent = 100;
			}

			adc2Capacitor_convert_flag = 1;
			HAL_ADC_Start_IT(&hadc2);
		}
	}

	void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
	    {
	        if (huart->Instance == USART2)
	        {
	            uint8_t received_char = RxData[0];

	            // detecting the end of the command
	            if (received_char == '\r' || received_char == '\n')
	            {
	                // end command buffering, terminating character
	                if (RxIndex < CMD_BUFFER_SIZE) {
	                    RxBuffer[RxIndex] = '\0';
	                    cmd_ready_flag = 1;
	                } else {
	                    // Buffer overflow - reset
	                    RxIndex = 0;
	                }
	            }

	            // standard character buffering
	            else if (RxIndex < CMD_BUFFER_SIZE - 1)
	            {
	                RxBuffer[RxIndex] = received_char;
	                RxIndex++;
	            }
	            else {
	                // Buffer full - ignore character or reset
	                RxIndex = 0;
	            }

	            // auscultation should be resumed at the next sign
	            HAL_UART_Receive_IT(huart, RxData, 1);
	        }
	    }

	void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	    if (htim->Instance == TIM7) {
	    	ms_elapsed_charging++;
	    }
	    else if (htim->Instance == TIM15) {
	    	s_elapsed_running++;
	    }
	}

	void ProcessCommand(char *cmd)
		{
		    // variables to be parsed
		    char 	command_str[10];
		    int 	value,
					items_read;

		    // searching for the *command number* format
		    items_read = sscanf(cmd, "%s %d", command_str, &value);

		    // I. pwm command support - changing lcd contrast
		    if (items_read >= 2 && strcmp(command_str, "pwm") == 0)
		    {
		        // checking the allowed pwm range
		        // in our case Counter Period ARR is 999
	        if (value >= 0 && value < PWM_MAX_VALUE)
	        	// setting a new fill factor for channel 3 TIM1
	            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, value);
	        else
	            pwm_not_allowed_value_flag = 1;
		    }

		    else if(items_read >= 2 && strcmp(command_str, "charge") == 0)
		    {
		    	Charge(value);
		    }

		    // II. start command support - start of measurements
		    else if (strcmp(command_str, "start") == 0)
		    {
		    	StartDischarging();
		    }

		    // III. stop command support - interruption of measurements
		    else if (strcmp(command_str, "stop") == 0)
		    {
		    	StopDischarging();
		    }
		    // IV. full command support - capacitor charging
		    else if (strcmp(command_str, "full") == 0)
		    {
	    	Charge(CHARGE_FULL_MAGIC);
		    }
		}
	float PredictDischargeTimeMs(uint32_t adc_raw)
	{
	    float curr_dv = start_state - adc_raw;
	    // Protection against division by zero
	    if (curr_dv <= 0.01f) {
	        return 0.0f;
	    }
	    float t_sec = adc_raw/curr_dv*s_elapsed_running;
	    if (t_sec < 0) t_sec = 0;
	    return t_sec;
	}

	void StartDischarging(void) {
		if (charging_active_flag == 0) {
			if (!discharging_active_flag) {
				start_state = current_percent;
				start_state_energy = current_energy;
				discharging_active_flag = 1;
				HAL_ADC_Start_IT(&hadc1);
				// three different uc's charging variants
				HAL_GPIO_WritePin(GPIOC, IC_ENABLE_Pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(GPIOC, COIL_ENABLE_Pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(GPIOC, PMOS_NENABLE_Pin, GPIO_PIN_RESET);
				__HAL_TIM_SET_COUNTER(&htim15, 0);
				HAL_TIM_Base_Start_IT(&htim15);
				s_elapsed_running = 0;
			}
		}
	}

	void StopDischarging(void) {
		// adc1 shutdown
		adc1Resistor_convert_flag = 0;
		// stop discharching
		discharging_active_flag = 0;
		HAL_ADC_Stop_IT(&hadc1);
		HAL_GPIO_WritePin(GPIOC, IC_ENABLE_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOC, COIL_ENABLE_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(GPIOC, PMOS_NENABLE_Pin, GPIO_PIN_SET);
		HAL_TIM_Base_Stop_IT(&htim15);
		HAL_Delay(5000);
		float d_energy = start_state_energy - current_energy;
        snprintf(txbufUARTresistor, sizeof(txbufUARTresistor), "log$---STATS---\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)txbufUARTresistor, strlen(txbufUARTresistor), UART_TIMEOUT_MS);
        snprintf(txbufUARTresistor, sizeof(txbufUARTresistor), "log$dE = %.2f J\r\n", d_energy);
        HAL_UART_Transmit(&huart2, (uint8_t*)txbufUARTresistor, strlen(txbufUARTresistor), UART_TIMEOUT_MS);
        snprintf(txbufUARTresistor, sizeof(txbufUARTresistor), "log$dt = %u s\r\n", (unsigned int)s_elapsed_running);
        HAL_UART_Transmit(&huart2, (uint8_t*)txbufUARTresistor, strlen(txbufUARTresistor), UART_TIMEOUT_MS);
        snprintf(txbufUARTresistor, sizeof(txbufUARTresistor), "log$---STATS---\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)txbufUARTresistor, strlen(txbufUARTresistor), UART_TIMEOUT_MS);


	}

	void Charge(int value) {
		// Prevent charging if value is negative (except for CHARGE_FULL_MAGIC)
		if (value < 0 && value != CHARGE_FULL_MAGIC) return;
		
		// Prevent charging during discharge
		if (discharging_active_flag == 1) return;
		
		if ((MAX_ENERGY < current_energy + value) && (value != CHARGE_FULL_MAGIC)) return;

        if (charging_active_flag == 0)
        {

            float start_v = current_voltage_cap_mV / 1000.0f;
            float target_v = sqrt((2 * (current_energy + value)) / CAPACITY);
            if (value == CHARGE_FULL_MAGIC) target_v = V_MAX_MV/1000;
            // if the capacitor is initially charged above the expected value,
            // do not turn on the charging
            if (start_v >= target_v) return;

            float charging_time_sec = (CAPACITY * (target_v - start_v)) / CHARGE_CURRENT;
            // empathetic value
            charging_time_sec *= CHARGE_TIME_MARGIN;

            target_charging_time_units = (uint32_t)charging_time_sec * 1000;

            __HAL_TIM_SET_COUNTER(&htim7, 0);
            ms_elapsed_charging = 0;
            HAL_TIM_Base_Start_IT(&htim7);
            HAL_GPIO_WritePin(GPIOB, BQ25173_CE_Pin, GPIO_PIN_RESET);
            charging_active_flag = 1;
        }

	}

	/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
