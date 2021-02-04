changes to test at wextcontrol
* wifi change
* always id change
* always test change
* rfid ip



´´´
I (11:25:20.380) NETWORK : IP4 Address string: 10.0.7.143, 10
W (11:25:20.386) CONNECTIVITY: : Initializing SNTP after first network connection
I (11:25:20.400) ZNTP     : Initializing SNTP
I (11:25:20.406) ZNTP     : Waiting for system time to be set... (1/60), status: 0
W (11:25:21.221) MCU: Dataset: T_EM: -0.00 -0.00 0.00  T_M: 137.24 90.19   V: -0.00 -0.00 -0.00   I: -0.00 -0.00 -0.00  0.0W 0.000Wh CM: 12  COM: 1 Timeouts: 0, Off: 0
I (11:50:38.546) ZNTP     : Notification of a time synchronization event
I (11:50:38.713) PROD-TEST :: waiting for RFID
I (11:50:39.624) ZNTP     : The sensible time is: 2021-02-03 11:50:39
W (11:50:39.886) MCU: Dataset: T_EM: -0.00 -0.00 0.00  T_M: 137.24 90.19   V: -0.00 -0.00 -0.00   I: -0.00 -0.00 -0.00  0.0W 0.000Wh CM: 12  COM: 1 Timeouts: 0, Off: 0
I (11:50:39.996) I2C_DEVICES: Temp: -16.18C Hum: 16.47%, Time is: 2021-02-03 11:50:39
W (11:50:40.003) ZNTP     : NTP synced time read from RTC: 2021-02-03 11:50:39
W (11:50:41.346) MCU: Dataset: T_EM: -0.00 -0.00 0.00  T_M: 137.24 90.19   V: -0.00 -0.00 -0.00   I: -0.00 -0.00 -0.00  0.0W 0.000Wh CM: 12  COM: 1 Timeouts: 0, Off: 0
Card detected! (Rx off version)
Single UID
Part 1: 92 BD A9 3B BD - Valid BBC BD == BD
SINGLE UID: 92 BD A9 3B


I (11:50:42.607) PROD-TEST :: nfctag submitted to prodtest procedure
I (11:50:42.608) PROD-TEST :: using rfid tag: nfc-92BDA93B
I (11:50:42.614) PROD-TEST :: Finding id from http://10.0.1.15:8585/get/mac
D (11:50:42.635) TRANS_TCP: [sock=54],connecting to server IP:10.0.1.15,Port:8585...
D (11:50:42.655) PROD-TEST :: HTTP_EVENT_ON_CONNECTED
D (11:50:42.664) PROD-TEST :: HTTP_EVENT_HEADER_SENT
W (11:50:42.786) MCU: Dataset: T_EM: -0.00 -0.00 0.00  T_M: 137.56 90.19   V: -0.00 -0.00 -0.00   I: -0.00 -0.00 -0.00  0.0W 0.000Wh CM: 12  COM: 1 Timeouts: 0, Off: 0
D (11:50:42.967) PROD-TEST :: HTTP_EVENT_ON_HEADER, key=Content-Type, value=text/plain
D (11:50:42.968) PROD-TEST :: HTTP_EVENT_ON_HEADER, key=Date, value=Wed, 03 Feb 2021 11:50:42 GMT
D (11:50:42.974) PROD-TEST :: HTTP_EVENT_ON_HEADER, key=Connection, value=keep-alive
D (11:50:42.982) PROD-TEST :: HTTP_EVENT_ON_HEADER, key=Transfer-Encoding, value=chunked
D (11:50:42.990) PROD-TEST :: HTTP_EVENT_ON_DATA, len=68
D (11:50:42.996) PROD-TEST :: read_len = 68
I (11:50:42.999) PROD-TEST :: v: 1, id: ZAP000040, psk: H+IHOeRKPLyh3F8+EXw7hVsbhO/pPqq5YXc4XN9wzME=, pin: 0975
W (11:50:44.246) MCU: Dataset: T_EM: -0.00 -0.00 0.00  T_M: 137.24 90.19   V: -0.00 -0.00 -0.00   I: -0.00 -0.00 -0.00  0.0W 0.000Wh CM: 12  COM: 1 Timeouts: 0, Off: 0
I (11:50:44.575) CONNECTIVITY: : starting cloud listener with , ,
I (11:50:44.576) Cloud Listener: Connecting to IotHub
I (11:50:44.577) Cloud Listener: mqtt connection:
 > uri: mqtts://zapcloud.azure-devices.net
 > port: 8883
 ´´´
