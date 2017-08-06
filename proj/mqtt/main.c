/*=============================================================================+
|                                                                              |
| Copyright 2015                                                             |
| Montage Inc. All right reserved.                                             |
|                                                                              |
+=============================================================================*/
/*! 
*   \file app_init.c
*   \brief main entry
*   \author Montage
*/

/*=============================================================================+
| Included Files                                                               |
+=============================================================================*/
#include <c_types.h>
#include <stdint.h>
#include <event.h>
#include <common.h>
#include <os_api.h>
#include <net_api.h>
#include <wla_api.h>
#include <cfg_api_new.h>
#include <gpio.h>
#include <version.h>
#include <built_info.h>
#include <omniconfig.h>

#if defined(CONFIG_FREERTOS)
#include <FreeRTOS.h>
#include <task.h>
#endif
#if defined(CONFIG_LWIP)
#include <net_api.h>
#endif
#include <user_config.h>
#include <mylinks_wifi.h>
#include <MQTTClient.h>
/*=============================================================================+
| Define                                                                       |
+=============================================================================*/



/*----------------------------------------------------------------*/
/**
 * 此例中使用没有无SSL连接方式,请在查看代码前做以下工作:
 * 将include文件夹中的“#define CONFIG_AXTLS 1”注释
 * make lib，重新编译mqtt.a静态库
 * make mqtt进行例程编译，生成mqtt.img文件
 */
/*----------------------------------------------------------------*/



static Network n;
static MQTTClient client;
static char *testStr = "Only Test";

static uint8_t sendbuf[1000];
static uint8_t readbuf[1000];

#define MYLINKS_SUB_TOPIC "mylinks/s"
#define MYLINKS_PUB_TOPIC "mylinks/p"

/*----------------------------------------------------------------*/
/**
 * The function is called once application start-up. Users can initial
 * structures or global parameters here.
 *
 * @param None.
 * @return int Protothread state.
 */
/*----------------------------------------------------------------*/
int app_main(void)
{
#ifdef CONFIG_WLA_LED
	pin_mode(WIFI_LED_PIN, 1);
	digital_write(WIFI_LED_PIN, 0);
#endif
	/* Do not add any process here, user should add process in user_thread */
	hw_sys_init();
	return PT_EXITED;
}





void uart_init(void)
{
	/*
	设置串口参数：
	参数1->波特率：115200
	参数2->校验位：无
	参数3->停止位：1位
	参数4->串口号：0号串口
	*/
	serial_conf(baudrate_select(115200), 0, 1, 0,0 );
	serial_init(0);

}

//串口接收回调函数
void test_uart0_rev( void * arg){
	uint8_t temp;
	extern struct serial_buffer *ur0_rxbuf;
	for(;;){
		//等待串口是否有数据传入
		if(0 != uart_recv_sem_wait(portMAX_DELAY)){
			continue;
		}
		//判断串口数据是否为空
		while(serial_buffer_empty(ur0_rxbuf)){
			//读取一个字节的串口数据
			temp = serial_buffer_getchar(ur0_rxbuf);
		}
	}
}

//接收数据回调函数
void MQTT_recv_data(MessageData* md)
{
	MQTTMessage* message = md->message;
	MQTTString * topicName = md->topicName;
	int i;

	if (strncmp(topicName->lenstring.data, MYLINKS_SUB_TOPIC, topicName->lenstring.len) == 0)
	{
		uart0_tx_buffer(message->payload,message->payloadlen);
	}
	return;
}

//发布数据函数
int MQTT_send_data(void)
{
	int len;
	MQTTMessage message;
	len = strlen(testStr);
	/* Send message */
	message.payload = testStr;
	message.payloadlen = len;

	message.dup = 0;
	message.qos = QOS1;
	message.retained = 0;

	return MQTTPublish(&client, MYLINKS_PUB_TOPIC, &message);
}


void MQTT_deinit(void)
{
	if (client.isconnected){
		MQTTDisconnect(&client);
	}
	MQTTClientDeinit(&client);
	FreeRTOS_closesocket(&n);
	return;
}


int MQTT_init(void)
{
	int rc = 0;
	FreeRTOS_NetworkInit(&n);
	//只需要填写MQTT协议的服务器IP或者域名，端口号。
	rc = FreeRTOS_NetworkConnect(&n, "app.mqlinks.com", 1883, 0, 0, 0, 0, 0, 0);
	if (rc < 0) {
		rc = FAILURE;
		goto exit;
	}
	//注册MQTT发送和接收的缓存
	MQTTClientInit(&client, &n, 2000, sendbuf, sizeof(sendbuf), readbuf, sizeof(readbuf));
	MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

	/***注意：这里请自行修改，以免与其它用户的ID号重命名***/
	data.clientID.cstring = "mylinks_000001";
	data.MQTTVersion = 3;
	//MQTT服务存在账号和密码，填写账号和密码
	data.username.cstring = "mylinks";
	data.password.cstring = "mylinks_20160915";
	//如果无账号密码。则使用NULL
	//data.username.cstring = NULL;
	//data.password.cstring = NULL;
	data.keepAliveInterval = 30;//30s
	data.cleansession = 1;
	data.willFlag = 0;
	data.will.topicName.cstring = "mylinks/will";
	data.will.message.cstring = "mylinks will message";
	data.will.retained = 0;
	data.will.qos = 0;
	//连接MQTT服务器
	rc = MQTTConnect(&client, &data);
	if (rc == FAILURE) {
		goto exit;
	}
	//设置MQTT的订阅号以及订阅号的回调函数
	rc = MQTTSubscribe(&client, MYLINKS_SUB_TOPIC, QOS1, MQTT_recv_data);
	if (rc == FAILURE) {
		goto exit;
	}
	return rc;
exit:
	MQTT_deinit();

	return rc;
}



//MQTT处理任务
static void  mqttclient( void *arg )
{
	int rc = FAILURE;
	for(;;){

		//等待STA获取IP地址
		if(get_slinkup() != STA_LINK_GET_IP){
			goto MQTT_ERR;
		}
		if(rc == FAILURE){
			rc = MQTT_init();
		}
		//如果rc创建失败,则继续创建
		if(rc == FAILURE){
			goto MQTT_ERR;
		}
		//发送一个MQTT的数据
		if(MQTT_send_data() < 0){
			//用户可自行处理，这里为断开后再次连接
			MQTT_deinit();
			goto MQTT_ERR;
		}

		//MQTT keepactive 重连接处理等处理，用户可以不用理会此处设置延时1000ms
		if (MQTTYield(&client, 1000) == FAILURE){
			//用户可自行处理，这里为断开后再次连接
			MQTT_deinit();
			rc = FAILURE;
			continue;
		}
MQTT_ERR:
		//此任务循环时间为100ms
		sys_msleep(100);
	}
exit:
    vTaskDelete(NULL);
	return;
}

#define SSID "amily"
#define PWD	 "xu841007"



void user_init(void){
	struct station_config s;
	uart_init();
	//注册一个串口0的接收任务进行数据接收
	uart0_rev_register(test_uart0_rev);
	//设置模块为STA工作模式
	wifi_set_opmode(OPMODE_STA);
	//读取当前模块的STA配置信息
	wifi_station_set_config(&s);
	if(strcmp(s.ssid,SSID) ||
		strcmp(s.password,PWD)){

		memset(&s,0,sizeof(s));
		//设置连接的路由器ssid
		strcpy(s.ssid,SSID);
		//设置连接的路由器密码
		strcpy(s.password,PWD);
		wifi_station_set_config(&s);
	}

	xTaskCreate(mqttclient, "mqtt", TASK_HEAP_LEN, 0, 5, NULL);
	return;
}



