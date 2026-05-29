#include <math.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/nus.h>
#include <stdio.h>
#include <string.h>




/*УСТРОЙСТВА*/
#define I2C_NODE DT_NODELABEL(i2c1)
#define GPIO_NODE DT_NODELABEL(gpio0)
/*MT6701 УГОЛ*/
#define MT6701_ADDR 0x06
#define MT6701_ANGLE_REG 0x03
/*ПИНЫ*/
#define POWER_PIN 13
#define HALL_PIN 22
#define LED0_PIN 15
/*BT*/
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME


static const struct device *i2c = DEVICE_DT_GET(I2C_NODE);
static const struct device *gpio = DEVICE_DT_GET(GPIO_NODE);
static const struct device *bmp= DEVICE_DT_GET(DT_NODELABEL(bmp180));


//	ADC
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));


struct sensor_value temperature, press;
uint8_t buf[2];

int32_t read_battery_voltage(void) {
    int err;
    int16_t sample_buffer;
    int32_t val_mv; 
       
    struct adc_sequence sequence = {
        .buffer = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
    };
    adc_sequence_init_dt(&adc_channel, &sequence);

   
    err = adc_read_dt(&adc_channel, &sequence);
    if (err < 0) {
        printk("Could not read ADC (%d)\n", err);
        return -1;
    }

    val_mv = sample_buffer;
    
    err = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
    if (err < 0) {
        printk("Error converting raw to mV (%d)\n", err);
        return -1;
    }

    
    return val_mv * 5;
   
}

//	BT

static bool ble_is_enabled = true;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, sizeof(DEVICE_NAME) - 1),
};

static struct k_work_delayable adv_work;

static void start_adv(void)
{
    
   
	(void)bt_le_adv_stop();    
    int err=bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err == -EALREADY) {        
        return;
    }
    if (err) {        
        k_work_schedule(&adv_work, K_MSEC(1000));
    }
}

static void adv_work_handler(struct k_work *work)
{
    start_adv();
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    
    if (err) {
        k_work_schedule(&adv_work, K_MSEC(50));
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    k_work_schedule(&adv_work, K_MSEC(50));
}

static void notif_enabled(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);    
    printk("Notifications: %s\n", enabled ? "Enabled" : "Disabled");
}

BT_CONN_CB_DEFINE(conn_cbs) = {
    .connected = connected,
    .disconnected = disconnected,
    
};
static struct bt_nus_cb nus_cb = {
    .notif_enabled = notif_enabled,
    
};

//          GPIO        

static struct gpio_callback hall_cb;
static volatile uint32_t pulse_count = 0;
static volatile uint32_t pulse_count_1min= 0;
static volatile uint32_t pulse_count_5min = 0;


// 	Обработчик прерывания

static struct k_work_delayable led_off_work;

static void hall_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
   	
	pulse_count++;
		
		
}






static void led_off_handler(struct k_work *work)
{
    gpio_pin_set(gpio, LED0_PIN, 0);
}

static void error_blink(int code) {
   while (1){
	for (int i = 0; i < code; i++) {
        gpio_pin_set(gpio, LED0_PIN, 1); /* Светодиод включён */
        k_sleep(K_MSEC(600));
        gpio_pin_set(gpio, LED0_PIN, 0); /* Светодиод выключен */  
		k_sleep(K_MSEC(600));      
    }
    /* Пауза между повторениями */
    k_sleep(K_MSEC(3000));
}
}


static void system_init(){

	int err;	
	if (!device_is_ready(gpio)	) {
		printk("GPIO not ready\n");
		error_blink(1);		
	}
	/*GPIO*/
	gpio_pin_configure(gpio, POWER_PIN, GPIO_OUTPUT_ACTIVE);	
	gpio_pin_configure(gpio, HALL_PIN, GPIO_INPUT | GPIO_PULL_UP);		
	gpio_pin_interrupt_configure(gpio, HALL_PIN, GPIO_INT_EDGE_FALLING);	
	gpio_pin_configure(gpio, LED0_PIN, GPIO_OUTPUT_INACTIVE);	
	k_work_init_delayable(&led_off_work, led_off_handler);
	gpio_init_callback(&hall_cb, hall_isr, BIT(HALL_PIN));
    gpio_add_callback(gpio, &hall_cb);
	

	if (!device_is_ready(i2c)) {
		printk("I2C device not ready\n");
		error_blink(2);			
	}
	
	if (!device_is_ready(bmp)) {
		printk("BMP180 device not ready\n");
		error_blink(3);		
	}

	if (!adc_is_ready_dt(&adc_channel)) {
        printk("ADC driver is not ready\n");
		error_blink(4);	        
    }

	
	

	

	/*BT*/
	
	err=bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
		error_blink(5);	        
    }

	err = bt_nus_cb_register(&nus_cb, NULL); //регистрирует callback BT NUS
    if (err) {
        printk("NUS callback register failed: %d\n", err);
		error_blink(6);	        
    }
	k_work_init_delayable(&adv_work, adv_work_handler);
    start_adv();
	/*ADC*/

	err = adc_channel_setup_dt(&adc_channel);
    if (err < 0) {
        printk("Could not setup ADC channel (%d)\n", err);
		error_blink(7);	        
    }

}

static float angle = 0;
static double temperature_double;
static double press_double;
static int64_t last_read=0;
static int32_t vbatt = 0;

static void read_sensors(int64_t now)
{
	if (last_read==0 ||(now-last_read)>=60000) {
		
	//BMP180
	int ret = sensor_sample_fetch(bmp);
	if (ret) {
		printk("BMP fetch error: %d\n", ret);				
	} else {
		sensor_channel_get(bmp, SENSOR_CHAN_DIE_TEMP, &temperature);
		sensor_channel_get(bmp, SENSOR_CHAN_PRESS, &press);
		temperature_double = sensor_value_to_double(&temperature);
		press_double = sensor_value_to_double(&press);
	}
	
	
	last_read=now;	

	char msg[32];					
				
	snprintf(msg, sizeof(msg), "S%d;%d;%d;\n",
		(int)(temperature_double*10), // Температуру делить на 10 в приемнике	
		(int)(press_double* 7.50062),
		vbatt); 
		// угол, температура,давление,
	int nus_err = bt_nus_send(NULL, msg, strlen(msg));
	if (nus_err && nus_err != -ENOTCONN) {
		printk("BLE send error: %d\n", nus_err);
	}

	}
	
}



int main(void)
{	
	
		
	
	
	float  rotation_time;
	int64_t last_1min = k_uptime_get();
	int64_t last_5min = k_uptime_get();
	float freq_5min=0, freq_1min=0;
		

	system_init();
    
	
	
	while (1) {		
		
		

		vbatt = read_battery_voltage();
		if (vbatt<3250){
			if (ble_is_enabled){
				bt_le_adv_stop();
			    ble_is_enabled = false;
			}
        	k_sleep(K_MSEC(60000));  
        	continue;	
			
		} else 
		{
			if (!ble_is_enabled){
				start_adv();
				ble_is_enabled=true;
			}
		} 
		
		if (vbatt > 3700) {
			rotation_time=6.0f;
			k_sleep(K_MSEC(6000));		
		} else {
			rotation_time=30.0f;
			k_sleep(K_MSEC(30000));
		} 
		/*gpio_pin_set(gpio, LED0_PIN, 1);
		k_work_schedule(&led_off_work, K_MSEC(50));	*/

		
			
		uint32_t pulses_temp=pulse_count;

		pulse_count_1min+=pulses_temp;
		pulse_count_5min+=pulses_temp;

		float freq_current = (float)pulses_temp/rotation_time;			
		pulse_count=0;

		int64_t now = k_uptime_get();

		if ((now-last_1min)>=60000){
			freq_1min = (float)pulse_count_1min/60.f;
			pulse_count_1min=0;
			last_1min=now;
		}

		if ((now-last_5min)>=300000){
			freq_5min = (float)pulse_count_5min/300.f;
			pulse_count_5min=0;
			last_5min=now;
		}

		
		gpio_pin_set(gpio, POWER_PIN, 1);
		k_sleep(K_MSEC(50));	
		read_sensors(now);	
		
		// Отправка 
		char msg[32];		
		//MT6701
		int ret=i2c_burst_read(i2c, MT6701_ADDR, MT6701_ANGLE_REG, buf, 2);
		if (ret) {
			printk("MT6701 read error: %d\n", ret);				
		} else{
			uint16_t raw = ((uint16_t)buf[0] << 6) | (buf[1] >> 2);
			angle = (float)raw / 16384.0f * 360.0f;
		}
		//snprintf(msg, sizeof(msg), "1000;1000;1000;1000\n"); 
		gpio_pin_set(gpio, POWER_PIN, 0);
		
		k_sleep(K_MSEC(500));

		snprintf(msg, sizeof(msg), "F%d;%d;%d;%d\n",(int)(freq_current*100),(int)(freq_1min*100),(int)(freq_5min*100),(int)angle); 
		
				
		/*snprintf(msg, sizeof(msg), "%d;%d;%d;%d;%d;%d;%d;\n",
				(int)angle,
				(int)(temperature_double*10), // Температуру делить на 10 в приемнике	
				(int)(press_double* 7.50062),
				(int)(freq_current*100), // Частоту делить на 100
				(int)(freq_1min*100),
				(int)(freq_5min*100),
				vbatt); 
				// угол, температура,давление,частота, частота 1 мин, частота 5 мин, батарея	

			*/	
		int nus_err = bt_nus_send(NULL, msg, strlen(msg));
		if (nus_err && nus_err != -ENOTCONN) {
			printk("BLE send error: %d\n", nus_err);
		}

				
		

	}
}
