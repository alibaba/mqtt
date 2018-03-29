/*******************************************************************************
 * Copyright (c) 2012, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial contribution
 *******************************************************************************/

#include "MQTTProtocolClient.h"
#include "Log.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "MQTTClient.h"
#include "MqttPushClient.h"
#include "Thread.h"
#include "md5.h"
#define WILL_MESSAGE_MIN_LEN 10
#define WILL_MESSAGE_MAX_LEN 1024
#define RETRY_INTERVAL 5000 //ms
 
static thread_type g_ThreadHandle = NULL;
static sem_type g_semWaitfor = NULL;
//#define printf LOGD
volatile MQTTClient_deliveryToken deliveredtoken;
struct _OPTIONS
{
	 char* tid;
	 char* password;
	 char* address;
	 MQTTClient_willOptions *will;
}gOPTIONS;
enum _eSER_STATUS
{
	MQTT_THREAD_CLOSE = 0,
	MQTT_THREAD_PAUSE,
	MQTT_THREAD_RUN
}eServiceStatus;
static MQTTClient mClient = 0;
mqttConnectionLost g_lost = NULL;
mqttMessageArrived g_arrive = NULL;
mqttIsDeviceConnectedNetwork g_isConnected = NULL;
MQTTClient_willOptions g_wopts =  MQTTClient_willOptions_initializer;
extern "C" mqttConnectionSuccess g_success ;
#ifdef WIN32
int UTF8ToGBK(unsigned char * lpUTF8Str,unsigned char * lpGBKStr,int nGBKStrLen)
{
	wchar_t * lpUnicodeStr = NULL;
	int nRetLen = 0;

	if(!lpUTF8Str)  //如果UTF8字符串为NULL则出错退出
		return 0;

	nRetLen = ::MultiByteToWideChar(CP_UTF8,0,(char *)lpUTF8Str,-1,NULL,NULL);  //获取转换到Unicode编码后所需要的字符空间长度
	lpUnicodeStr = new WCHAR[nRetLen + 1];  //为Unicode字符串空间
	nRetLen = ::MultiByteToWideChar(CP_UTF8,0,(char *)lpUTF8Str,-1,lpUnicodeStr,nRetLen);  //转换到Unicode编码
	if(!nRetLen)  //转换失败则出错退出
		return 0;

	nRetLen = ::WideCharToMultiByte(CP_ACP,0,lpUnicodeStr,-1,NULL,NULL,NULL,NULL);  //获取转换到GBK编码后所需要的字符空间长度

	if(!lpGBKStr)  //输出缓冲区为空则返回转换后需要的空间大小
	{
		if(lpUnicodeStr)
			delete []lpUnicodeStr;
		return nRetLen;
	}

	if(nGBKStrLen < nRetLen)  //如果输出缓冲区长度不够则退出
	{
		if(lpUnicodeStr)
			delete []lpUnicodeStr;
		return 0;
	}

	nRetLen = ::WideCharToMultiByte(CP_ACP,0,lpUnicodeStr,-1,(char *)lpGBKStr,nRetLen,NULL,NULL);  //转换到GBK编码

	if(lpUnicodeStr)
		delete []lpUnicodeStr;

	return nRetLen;
}
#endif

void delivered(void *context, MQTTClient_deliveryToken dt)
{
 
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    
    char* payloadptr = NULL;

	if(message)
    {
		payloadptr = (char *)message->payload;
		if(g_arrive)
		{
			g_arrive(payloadptr,message->payloadlen);
		}
	}
#ifdef WIN32
	if(payloadptr)
	{
		char szUTF8Text[2048] = {0};
		memcpy(szUTF8Text,payloadptr,message->payloadlen);
		char szGBKText[2048] = {0};
		int len = UTF8ToGBK((unsigned char *)szUTF8Text,(unsigned char *)szGBKText,2047);
		printf("\n======================\n = %s\n",szGBKText);
	}
	//static int i =1 ;
	//printf("%d ======================payloadptr\n",i++);
   // MqttCallbackmsgarrvd(topicName,payloadptr,message->payloadlen);
#endif
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause)
{
	Log(TRACE_MIN, -1, "Calling connectionLost for client");
	if (g_lost)
	{
		g_lost();
	}

    //printf("\n\nConnection lost\n");
    //printf("\n     cause: %s\n", cause);
	
	// mqttUninit();
    //MqttCallbackconnlost(cause);
}


MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
void LogCallback(enum LOG_LEVELS level, char* message)
{
	//printf("\nLogCallback message=%s\n", message);
	if(message && (strlen(message) + strlen("mqttSDK----") < 512))
	{
	char msg[512]={0};
	sprintf(msg,"mqttSDK----%s",message);
	//MqttCallbackOnLog(msg);
	}
}
void initOption(const char* tid,char* password,const char* address,const char*msg)
{
	int tidlength = strlen(tid);
	gOPTIONS.tid = (char*)malloc(tidlength+1);
	//memset(gOPTIONS.tid,strlen(tid)+1,0x00);
	memcpy(gOPTIONS.tid,tid ,tidlength);
	gOPTIONS.tid[tidlength] = 0;
	int passwordlength = strlen(password);
	gOPTIONS.password = (char*)malloc(passwordlength+1);
	//memset(gOPTIONS.password,strlen(password)+1,0x00);
	memcpy(gOPTIONS.password,password ,passwordlength);
	gOPTIONS.password[passwordlength] = 0;

	int addresslength = strlen(address);
	gOPTIONS.address = (char*)malloc(addresslength+1);
	//memset(gOPTIONS.address,strlen(address)+1,0x00);
	memcpy(gOPTIONS.address,address ,addresslength);
	gOPTIONS.address[addresslength]=0;
	gOPTIONS.will = &g_wopts;
	int iMsgLen = 0;
	if(msg)
	{ 
		iMsgLen =  strlen(msg);
	}
	if(iMsgLen >WILL_MESSAGE_MIN_LEN && iMsgLen < WILL_MESSAGE_MAX_LEN)
	{
			gOPTIONS.will->message = MQTTStrdup(msg);
			gOPTIONS.will->topicName = "autocpp";
	}
	 
}
void uninitOption()
{
	if(gOPTIONS.tid)
	{
		free(gOPTIONS.tid);
		gOPTIONS.tid = NULL;
	}
	if(gOPTIONS.password)
	{
		free(gOPTIONS.password);
		gOPTIONS.password = NULL;
	}
	if(gOPTIONS.address)
	{
		free(gOPTIONS.address);
		gOPTIONS.address = NULL;
	}
	if(gOPTIONS.will && gOPTIONS.will->message)
	{
		free(gOPTIONS.will->message);
		gOPTIONS.will->message = NULL;
	}
}
/* MD5加密 */
static int  MD5fun(char *sign,const char *str)
{
	int i = 0;   /* 索引 */
	unsigned char digest[16];
	md5_state_t stMD5;
	mqtt_md5_init(&stMD5);
	mqtt_md5_append(&stMD5, (const unsigned char*)str, (int)strlen(str));
	mqtt_md5_finish(&stMD5, digest);
	for (i = 0; i < 16; ++i)
	{
		(void)sprintf(&sign[i*2], "%x", ((char)digest[i] & 0xF0) >> 4);
		(void)sprintf(&sign[(i*2)+1], "%x", (char)digest[i] & 0x0F);
	}
	return(1);
}
bool mqttSetCallback(mqttConnectionLost cl,mqttMessageArrived ma,mqttIsDeviceConnectedNetwork n,mqttConnectionSuccess s)
 {
	g_lost = cl;
	g_arrive = ma;
	g_isConnected =  n;
	g_success = s; 
	return true;
 }
bool mqttInit(const char* ptid,const char* key,const char* paddress,const char *message)
{
	bool rc = false;
	do
	{
		if (mClient != 0)
		{
			break;
		}
		if (key == NULL)
		{
			break;
		}
		char client[256] = {0};
		strcat(client,ptid);
		strcat(client,"@");
		int iKeyOffset = 0;
		if(key[0] == '@')
		{
			iKeyOffset = 1;
		}
		strcat(client,key+iKeyOffset);
		char sign[33] = {0};
		MD5fun(sign,client);
		MQTTClient_init();
		//MQTTClient_deliveryToken token;
		if(ptid == NULL || strlen(sign) == 0 || paddress == NULL)
		{
			break;
		}
		initOption(ptid,sign,paddress,message);
		MQTTClient_create(&mClient, gOPTIONS.address, gOPTIONS.tid,
			MQTTCLIENT_PERSISTENCE_NONE, NULL);
		if(mClient == 0)
		{
			break;
		}
		conn_opts.keepAliveInterval = 8;
		conn_opts.connectTimeout = 10;
		conn_opts.cleansession = 0;
		conn_opts.username = gOPTIONS.tid;
		conn_opts.password = gOPTIONS.password;
		int iMsgLen = 0;
		if (message)
		{
			iMsgLen = strlen(message);
		}
		if(iMsgLen > WILL_MESSAGE_MIN_LEN && iMsgLen < WILL_MESSAGE_MAX_LEN)
		{
			conn_opts.will = gOPTIONS.will;
		}
		else
		{
			conn_opts.will = NULL;
		}
 		MQTTClient_setCallbacks(mClient, NULL, connlost, msgarrvd, delivered);
		g_semWaitfor = Thread_create_sem();
		rc = true;
	}while (0);
    return rc ;
}

thread_return_type WINAPI mqttThread(void* n)
{

	int rc = MQTTCLIENT_SUCCESS;
	int iSleepInterval = 500; //500ms
	int iSleepMaxInterval = 30000; //30s
	int iSleepTime = 0;
#ifndef WIN32
	pthread_setname_np(pthread_self(),"mqttSDK");
#endif
	do{
		if(eServiceStatus == MQTT_THREAD_RUN && mClient != 0 && !MQTTClient_isConnected(mClient))
		{
			if(g_isConnected)
			{
				if(g_isConnected() != true && iSleepTime < iSleepMaxInterval)
				{
					MQTTClient_sleep(iSleepInterval);
					iSleepTime += iSleepInterval;
					continue;
				}
				iSleepTime = 0;
			}
			rc = MQTTClient_connect(mClient, &conn_opts);
			if (rc != MQTTCLIENT_SUCCESS)
			{
				MQTTClient_disconnect(mClient,0);
				connlost(NULL,NULL);
			}
		}
        Thread_wait_sem(g_semWaitfor,RETRY_INTERVAL);
	}while (eServiceStatus != MQTT_THREAD_CLOSE);	
	return 0;
}
  
bool  mqttServiceStart()
{
	bool rc = true;
	if(eServiceStatus == MQTT_THREAD_CLOSE && g_ThreadHandle==NULL)
	{
		eServiceStatus = MQTT_THREAD_RUN;
		g_ThreadHandle = Thread_start(mqttThread,NULL);
	}
	return(rc);
}

 
void mqttServiceSuspend()
{
	eServiceStatus = MQTT_THREAD_PAUSE;
	MQTTClient_Suspend();
	 
}
void mqttServiceResume()
{
	eServiceStatus = MQTT_THREAD_RUN;
	Thread_post_sem(g_semWaitfor);
	MQTTClient_Resume();
}
void mqttServiceUninit()
{
	if(mClient == 0 || eServiceStatus == MQTT_THREAD_CLOSE)
	{
		return;
	}
	eServiceStatus = MQTT_THREAD_CLOSE;
	MQTTClient_Suspend();
	Thread_post_sem(g_semWaitfor);
	if(g_ThreadHandle != NULL)
	{

		Thread_join(g_ThreadHandle);
		g_ThreadHandle = NULL;
	}
	
	if(MQTTClient_isConnected(mClient))
	{
		MQTTClient_disconnect(mClient, 0);
	}
	MQTTClient_destroy(&mClient);
	uninitOption();
	MQTTClient_unInit();
	if(g_semWaitfor != NULL)
	{
		Thread_destroy_sem(g_semWaitfor);
		g_semWaitfor = NULL;
	}
	mClient = 0;
}
static mqttLogTraceCallback g_pTraceCallback = NULL;

static void LogTraceCallbackFunc(enum LOG_LEVELS level, char* message)
{
	if (g_pTraceCallback)
	{
		g_pTraceCallback((MQTT_Log_levels)level,message);
	}
};
void mqttLogSetTraceCallback(mqttLogTraceCallback callback,MQTT_Log_levels level)
{
	if(callback != NULL)
	{
		g_pTraceCallback = callback;
		Log_setTraceCallback(LogTraceCallbackFunc);
		Log_setTraceLevel((enum LOG_LEVELS)level);
	}
} 