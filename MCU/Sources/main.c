#include "derivative.h" /* include peripheral declarations */
#include "TFC/TFC.h"
#include "camera_processing.h"
#include "DistantIO/distantio.h"
#include "chrono.h"
#include "UnitTests/UnitTests.h"
#include "TFC/TFC_UART.h"

//TODO : Investiger erreurs avec virage+discontinuites

// WARNING : SysTick frequency is 50kHz. Will overflow after roughly 2.5 hours
// Derivative not clean

void cam_program();
void configure_bluetooth();

float max(float a, float b)
{
	if(a>b)
		return a;
	return b;
}


int main(void)

{
	//test_serial1();
	//test_protocol1();
	//test_distantio_minimal();
	
	cam_program();
	
	//configure_bluetooth();
	
	return 0;
}

//TODO : Calibrer offset

void cam_program()
{
	TFC_Init();
	//Init all 3 communication layers
	init_serial();
	init_protocol();
	init_distantio();
	
	cameraData data;
	
	init_data(&data);
	
	//Computation variables
	float previous_error = 0.f;
	float error_proportionnal = 0.f;
	float error_derivative = 0.f;
	
	float command = 0.f;
	float servo_offset = 0.2;
	
	float commandD = 0.f;
	float commandP = 0.f;
	int r = 0;
	
	//      PID        
	float P;
	float D;
	float command_engines;
	
	// Speed control
	
	float gear_1_threshold = 5;
	float gears[3];
	uint16_t current_gear = 0;
	gears[0] = 0.4f; // Line lost speed
	gears[1] = 0.5f; // Error > gear_1_threshold
	gears[2] = command_engines; //Full speed
	
	//Le plus performant pour l'instant
	P = 0.013;
	D = 0.00095f;
	command_engines = 0.f;
	
	//To check loop times
	chrono chr_cam_m, chr_loop_m,chr_io,chr_rest, chr_Task;
	//To ensure loop times
	chrono chr_distantio,chr_cam,chr_led,chr_servo;
	float t_cam = 0, t_loop = 0, t_io = 0, t_rest = 0, t_Task = 0;
	float looptime_cam;
	float period_cam;
	float queue_size = 0;
	
	uint32_t counter_fps = 0,fps = 0;
	chrono chrono_fps;
	
	uint32_t counter_process_fps = 0, process_fps = 0;
	chrono chrono_process_fps;
	
	//TFC_HBRIDGE_ENABLE;
	
	uint16_t pload;
	
	uint8_t led_state = 0;
	TFC_SetBatteryLED_Level(led_state);
	
	float exposure_time_us = 8000;
	float servo_update_us = 5000;
	
	//Readonly variables
	register_scalar(&command_engines,FLOAT,1,"command engines");
	register_scalar(&error_proportionnal, FLOAT,0,"Proportionnel");
	register_scalar(&error_derivative, FLOAT,0,"Derivative");
	register_scalar(&command,FLOAT,0,"command");
	register_scalar(&commandP,FLOAT,0,"cmd P");
	register_scalar(&commandD,FLOAT,0,"cmd D");
	//register_scalar(&data.position_left,FLOAT,0,"position left");
	//register_scalar(&data.position_right,FLOAT,0,"position right");
	//register_scalar(&data.error_left,FLOAT,0,"error left");
	//register_scalar(&data.error_right,FLOAT,0,"error right");
	//register_scalar(&data.one_edge_choice,INT32,0,"decision (1 right, -1 left)");
	
	//register_scalar(&data.linestate, INT32,0,"LineState");
	//register_scalar(&data.previous_line_position,FLOAT,0,"Variations position");
	//register_scalar(&data.current_linewidth, FLOAT,0,"Linewidth current");
	//register_scalar(&data.current_linewidth_diff, FLOAT,0,"Linewidth error");
	//register_scalar(&data.deglitch_counter, INT16,0,"deglitch");
	//register_scalar(&data.deglitch_limit, INT16,1,"deglicth limit");
	//register_scalar(&data.error, FLOAT,0,"Linewidth error");
	//register_scalar(&data.linewidth, FLOAT,0,"Linewidth");
	
	//register_scalar(&data.valid_line_position,FLOAT,0,"Shielded Error");
	
	//register_scalar(&data.hysteresis_threshold,FLOAT,1,"Protection distance");
	//register_scalar(&data.distance,FLOAT,1,"distance variation");
	
	
	//Read/write variables
	register_scalar(&P,FLOAT,1,"P");
	register_scalar(&D,FLOAT,1,"D");
	register_scalar(&exposure_time_us,FLOAT,1,"Exposure time (us)");
	register_scalar(&servo_update_us,FLOAT,1,"Servo update period (us)");
	register_scalar(&fps,UINT32,0,"FPS");
	register_scalar(&process_fps,UINT32,0,"FPS camera");
	
	register_scalar(&current_gear,UINT16,0,"gear");
	register_scalar(&gear_1_threshold,FLOAT,1,"full speed threshold");
	
	//Calibration data
	register_scalar(&servo_offset,FLOAT,1,"servo_offset");
	register_scalar(&data.linewidth,FLOAT,1,"linewidth");
	register_scalar(&data.offset,FLOAT,1,"line offset");
	register_scalar(&data.threshold,INT32,1,"line threshold");
	
	//Loop times
	register_scalar(&t_cam,FLOAT,0,"max camera processing time(ms)");
	register_scalar(&t_loop,FLOAT,0,"max main time(ms)");
	register_scalar(&period_cam,FLOAT,0,"camera period (ms)");
	register_scalar(&t_io,FLOAT,0,"distant io time(ms)");
	register_scalar(&t_Task,FLOAT,0,"max TFC task time(ms)");
	register_scalar(&looptime_cam,FLOAT,0,"cam exe period(us)");
	
	//Secondary values
	register_scalar(&pload,UINT16,0,"Serial load");
	register_scalar(&queue_size, FLOAT,0,"queue size");
	
	register_scalar(&data.edges_count,UINT16,0,"edge count");
	register_array(data.threshold_image,128,INT8,0,"threshold_line");
	register_array(data.derivative_image,128,INT32,0,"line derivative");
	register_array(data.derivative_zero,128,INT32,0,"line derivative zero");
	register_array(data.raw_image,128,UINT16,0,"raw_line");
	
	TFC_SetLineScanExposureTime(exposure_time_us);
	TFC_SetServo(0, servo_offset);
	
	reset(&chr_distantio);
	reset(&chr_led);
	reset(&chr_servo);
	reset(&chr_cam_m);
	reset(&chr_io);
	reset(&chr_rest);
	reset(&chr_Task);
	reset(&chrono_fps);
	reset(&chrono_process_fps);
	
	for(;;)
	{
			
		/**TIME MONITORING**/				reset(&chr_loop_m);					
		
		//TFC_Task must be called in your main loop.  This keeps certain processing happy (I.E. Serial port queue check)
		/**TIME MONITORING**/				reset(&chr_Task);
		TFC_Task();
		/**TIME MONITORING**/				t_Task = max(t_Task,ms(&chr_Task));
		
		/* -------------------------- */
		/* ---- UPDATE DISTANTIO ---- */
		/* -------------------------- */
		if(us(&chr_distantio) > 3000)
		{
			reset(&chr_distantio);
			
			reset(&chr_io);
			update_distantio();
			t_io = ms(&chr_io);
			pload = getPeakLoad();
			t_rest = 0;
			t_loop = 0;
			t_cam = 0;
		}
		
		//Compute fps
		if(ms(&chrono_fps) < 1000)
			counter_fps++;
		else
		{
			fps = counter_fps;
			counter_fps = 0;
			remove_ms(&chrono_fps,1000);
		}
				
		/* -------------------------- */
		/* - UPDATE LINE PROCESSING - */
		/* -------------------------- */
		looptime_cam = us(&chr_cam);
		if(looptime_cam > exposure_time_us)
		{
			reset(&chr_cam);
			period_cam = looptime_cam;
			
			//Compute fps
			if(ms(&chrono_process_fps) < 1000)
				counter_process_fps++;
			else
			{
				process_fps = counter_process_fps;
				counter_process_fps = 0;
				remove_ms(&chrono_process_fps,1000);
			}
			
			/**TIME MONITORING**/			reset(&chr_cam_m);
			
			r = read_process_data(&data);
			if(r == LINE_OK)
			{				
				//Compute errors for PD
				previous_error = error_proportionnal;
				error_proportionnal = data.line_position;
				error_derivative = (error_proportionnal - previous_error) * 1000000.f / looptime_cam;
				
				//Adjust gear depending on error
				if(error_proportionnal > gear_1_threshold || error_proportionnal < -gear_1_threshold)
				{
					current_gear = 1;
				}
				else
					current_gear = 2;
			}
			else
			{
				error_derivative = 0;
				//Reduce speed with delay on restart
				current_gear = 0;
			}
			
			
			
			TFC_SetLineScanExposureTime(exposure_time_us);
							
			/**TIME MONITORING**/			t_cam = max(t_cam,ms(&chr_cam_m));
		}	
		
		/* -------------------------- */
		/* ---- UPDATE DIRECTION ---- */
		/* -------------------------- */
		if(us(&chr_servo) > servo_update_us)
		{
			remove_us(&chr_servo,servo_update_us);
			
			commandP = P * error_proportionnal;
			commandD = D * error_derivative;
			
			//Get offset
			servo_offset = TFC_ReadPot(0);
			
			//Compute servo command between -1.0 & 1.0 
			command = commandD + commandP + servo_offset;
			
			if(command > 1.f)
				command = 1.f;
			if(command < -1.f)
				command = -1.f;
							
			TFC_SetServo(0, command);
		}		
		
		/* -------------------------- */
		/* ---   UPDATE ENGINES   --- */
		/* -------------------------- */
		if(TFC_GetDIP_Switch() == 0)
		{
			TFC_HBRIDGE_DISABLE;
			TFC_SetMotorPWM(0 , 0);
		}
		else
		{
			//Disable engines on low value
			if(command_engines > -0.1f && command_engines < 0.1f)
			{
				TFC_HBRIDGE_DISABLE;
				TFC_SetMotorPWM(0 , 0);
			}
			else
			{
				TFC_HBRIDGE_ENABLE;
				gears[2] = command_engines;
				if(current_gear > 2)
					current_gear = 0;
				TFC_SetMotorPWM(gears[current_gear] , gears[current_gear]);
			}
		}
		
		/* -------------------------- */
		/* --- UPDATE PERIPHERALS --- */
		/* -------------------------- */				
		//Calibration and engine update the rest of the time 
		if(TFC_PUSH_BUTTON_0_PRESSED)
		{
			calibrate_data(&data,exposure_time_us);
			led_state = 3;
			TFC_SetBatteryLED_Level(led_state);
		}
		
		/* -------------------------- */
		/* ---  UPDATE ALIVE LED  --- */
		/* -------------------------- */
		if(ms(&chr_led) > 500)
		{
			reset(&chr_led);
			led_state ^= 0x01; 	
			TFC_SetBatteryLED_Level(led_state);
		}
		
		
		
		
		/**TIME MONITORING**/				reset(&chr_rest);
		/**TIME MONITORING**/				t_loop = max(t_loop,ms(&chr_loop_m));
		/**TIME MONITORING**/				t_rest = max(t_rest,ms(&chr_rest));
	}
}

void configure_bluetooth()
{
	TFC_Init();
		
		uart0_init (CORE_CLOCK/2/1000, 38400);
		 for(;;) //Try AT Command
		 {   
			 TFC_Task();
			
			 
			 if(TFC_Ticker[0]>1000)
			 {                   
				 
				 
				 serial_printf("AT+UART=115200,0,0\r\n");
				 TFC_Ticker[0] = 0;
				
				 TFC_Task();
				 
				 //Reset uart 
				 //uart0_init (CORE_CLOCK/2/1000, 115200);
			 }
		 }
}

