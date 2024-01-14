//********************************************************************************************************
// BLE Connect to a Shelly 1PM Switch - Via a Particle IoT BLE Device
// Allow monitoring of Switch's state/voltage/current/power
// Allow operating the Switch
// Receive plublished message about Switch State Change
//
// written by:  Terje B. Nilsen
// version:     1.3
// date:        14 JAN 2024
//
// NOTE: Many thanks to Konstantinos Xynos for showing me how to operate
//       a BLE link to a Shelly Device. I used and referred to his python code in
//       https://github.com/epicRE/shelly-smart-device/blob/main/shelly-ble.py
//
//********************************************************************************************************
// This was successfuly tested using a Particle's ARGON OS4.0.1 and BORON OS4.0.2
// It was also used on a PHOTON2 with OS5.6.0 (pre-release) which had problems (BLE connection)
//
// The following variables are exposed to the Particle Cloud:
//
// String sBLEstatus[NDEVICES]  = This shows if the BLE connection is made
// String sSwitch[NDEVICES]     = This is the Shelly switch state (O,OFF, or unknown)
// String sVoltage[NDEVICES]    = This is the measured voltage at the switch
// String sCurrent[NDEVICES]    = This is the measured current thru the switch
// String sPower[NDEVICES]      = This is the consumed power thru the switch
//
//  Refer to the setup() routine.
// 
// The following Particle cloud function is accessible to minipulate the switches
//
// functiona name:              Control
// string parameters to pass:   switch,operation (Switch number is zero-based)
//
// Examples parameters:     0,0  -> Switch[0], turn OFF
//                          0,1  -> Switch[0], turn ON
//                          1,1  -> Switch[1]  turn ON
//                          1,?  -> Switch[1]  query state. returned 0=OFF, 1=ON, -1=INVALID string sent
//
//
//  NOTE: Use "as is". I assume no liability in any manner whatsoever. That said, be safe and have fun!
//*******************************************************************************************************
#include "Particle.h"

//******************************
//Particle serial debug function
//******************************
SerialLogHandler logHandler(LOG_LEVEL_ALL);

//**************************************************
// Initialize the UUIDS for the Shelly Device switch
//**************************************************
BleUuid uuidRW          ("5F6D4F53-5F52-5043-5F64-6174615F5F5F");
BleUuid uuidREAD_NOTIFY ("5F6D4F53-5F52-5043-5F72-785F63746C5F");
BleUuid uuidW           ("5F6D4F53-5F52-5043-5F74-785F63746C5F");

//SYSTEM_MODE(MANUAL)

//***********************************************************
//***********************************************************
//***********************************************************
//These are the BLE MAC adresses of our Shelly devices
//NOTE: This is NOT the MAC address of the WiFi interface
//To obltain these adresses, open the webpage for the switch
//and go to the Bluetooth settings. The MAC address is at the
//top of the page.
//Also, make sure the Bluetooth is enabled for the Switch
//Make sure RPC under Bluetooth setting is also enabled.
//************************************************************
//***********************************************************
//***********************************************************
#define NDEVICES 2 //Number of Shelly Switches we are controlling
BleAddress ShellyAddress[NDEVICES]  = {"C049EF8CFCEE","C149EF8CFCEE"}; //Example connects to same switch
String sSwitchName[NDEVICES] = {"Hallway","Kitchen"};


//************************************************************************
//Here are some supported functions we can access on the Shelly 1PM Switch
//************************************************************************
const char read_device_info[]   = "{\"id\":211423784140012,\"method\":\"Shelly.GetDeviceInfo\"}";
const char write_switch_OFF[]   = "{\"id\":211423784140012,\"src\":\"shelly-app\",\"method\":\"switch.set\",\"params\":{\"id\":0,\"on\":false}";
const char write_switch_ON[]    = "{\"id\":211423784140012,\"src\":\"shelly-app\",\"method\":\"switch.set\",\"params\":{\"id\":0,\"on\":true}";
const char read_wifi_status[]   = "{\"id\":211423784140012,\"method\":\"Wifi.GetStatus\"}";
const char read_switch_status[] = "{\"id\":211423784140012,\"src\":\"shelly-app\",\"method\":\"switch.getstatus\",\"params\":{\"id\":0}";

//*********************************************
//Buffer for receiving messages from the switch
//*********************************************
uint8_t rxbuf[1024];
volatile int state[NDEVICES]; //state machine. All BLE access is done in the loop()

//*****************
//Bluetooth Objects
//*****************
BlePeerDevice       peer[NDEVICES];
BleCharacteristic   peerTxCharacteristic[NDEVICES];
BleCharacteristic   peerRxCharacteristic[NDEVICES];
BleCharacteristic   peerRxTxCharacteristic[NDEVICES];

//*******************************************
//Holds values from switches for cloud access
//*******************************************
String sVoltage[NDEVICES];
String sCurrent[NDEVICES];
String sPower[NDEVICES];
String sSwitch[NDEVICES];
String sPreSwitch[NDEVICES];
String sBLEstatus[NDEVICES];


//*****************
//Extract the value
//*****************
String ExtractValue(String ex, String s)
{
    String r;

    if (s.indexOf(ex) != -1)
    {
        r = s.substring(s.indexOf(ex));
        r = r.substring(r.indexOf(":")+1,r.indexOf(","));
    }
    
    return r;
}
//*******************************************************************
//Extract the voltage reading from response and return it as a String
//*******************************************************************
String GetVoltage(char *msg)
{
    String s = msg;
    String ex = "\"voltage\":";
    
    s = ExtractValue(ex, s);
    Log.info("***Voltage=%s<-***",s.c_str());
    return s;
}
//**************************************************************
//Extract the power reading from response and return as a String
//**************************************************************
String GetPower(char *msg)
{
    String s = msg;
    String ex = "\"apower\":";
    
    s = ExtractValue(ex, s);
    Log.info("***Power=%s<-***",s.c_str());
    return s;
}
//****************************************************************
//Extract the current reading from response and return as a String
//****************************************************************
String GetCurrent(char *msg)
{
    String s = msg;
    String ex = "\"current\":";
    
    s = ExtractValue(ex, s);
    Log.info("***Current=%s<-***",s.c_str());
    return s;
}
//*************************************************************
//Extract the switch state from response and return as a String
//*************************************************************
String GetSwitch(char *msg)
{
    String s = msg;
    String ex = "\"output\":";
    
    s = ExtractValue(ex, s);
    Log.info("***Switch=%s<-***",s.c_str());
    if (s == "true")
    {
        s = "ON";
    }
    else
    {
        s = "OFF";
    }
    return s;
}

//********************************
//Change endianess of passed value
//********************************
int change_endian(const uint8_t * v)
{
    uint8_t res[4];
    //Swap bytes
    res[0] = v[3];
    res[1] = v[2];
    res[2] = v[1];
    res[3] = v[0];
    return *(int *)res;
}
//************************************************
//Callback for RX data. Only used for diagniostics
//************************************************
void onDataReceived(const uint8_t* data, size_t len, const BlePeerDevice& peer, void* context) 
{
    if (len == 4)
    {
        Log.info("***DATA ON ITS WAY->%d<-***",change_endian(data));
    }
    else
    {
        Log.info("***DATA->len=%d<-***",len);
    }
}
//**********************************************************
// Callback when Shelly peripheral closes the BLE connection
//**********************************************************
void onDisconnect(const BlePeerDevice& peer, void* context)
{
    Log.info("***DISCONNECTED***");
    //System.reset(RESET_NO_WAIT); //Just like hitting the reset button
}
//******************************
//Write a message out a BLE link
//******************************
int TxBLEpacket(BleCharacteristic &pTx, BleCharacteristic &pRx, BleCharacteristic &pRxTx, const char * txbuf)
{
    int len = strlen(txbuf);
    
    //*******************************
    //Set correct endianess of length
    //*******************************
    len = change_endian( (uint8_t *) &len);
    //*******************************************
    //Send the length of the packet we're sending
    //*******************************************
    pTx.setValue((uint8_t *)&len,4);
    //***********************
    //Send the actual message
    //***********************
    pRx.setValue((uint8_t *)txbuf,strlen(txbuf));
    //**********************
    //Get length of response
    //**********************
    pRxTx.getValue((uint8_t *)&len,4);
    //****************************************
    //Set correct endianess of returned length
    //****************************************
    len = change_endian( (uint8_t *) &len);
    //**********************************************************************
    //Return number of bytes that device returned to us and needs to be read
    //**********************************************************************
    return len;
}
//**************************
//Read response via BLE link
//**************************
int RxBLEpacket(BleCharacteristic &pRx, uint8_t * rxb, int len, int buffersize)
{
    int rxlen =0; 
    //*************************************
    //Read the message from the BLE buffers
    //*************************************
    do
    {
        rxlen += pRx.getValue(&rxb[rxlen], len-rxlen);
    }
    while ( (rxlen+1) < len && rxlen < buffersize); //On fragmented packets We're off by one each time - maybe a string NULL deficit? This works though
    //*************************************************
    //Return number of bytes that device returned to us        
    //*************************************************
    return rxlen;
}
//**************************************
//Display a long message on the Log Info
//Log.Info() chops it at 154 characters
//**************************************
void long_log_message(char * msg)
{
    int len = strlen(msg), x = 0, i = 0;
    char s;

    //**************************************
    //Loop until entire message is displayed
    //**************************************
    do
    {
        if (len > 150)
        {
            s = msg[x+150]; //save character
            msg[x+150] = 0; //NULL terminate string
            Log.info("MSG[%d]->%s",++i,&msg[x]);
            len -= 150;
            msg[x+150] = s; //restore character
            x += 150;
        }
        else
        {
            //We are done
            Log.info("MSG[%d]->%s",++i,&msg[x]);
            len = 0; //exit loop
        }
    }
    while(len);
}
//********************************************
//Write command and Read return of BLE message
//********************************************
int WriteReadBLE(const char * msg, int sw)
{
    int ret = 1;
    
    int len = TxBLEpacket(peerTxCharacteristic[sw], peerRxCharacteristic[sw], peerRxTxCharacteristic[sw], msg);
    Log.info("RX LengthA %d",len);
    memset(rxbuf,0,sizeof(rxbuf)); //Be safe
    len = RxBLEpacket(peerRxCharacteristic[sw], rxbuf, len, sizeof(rxbuf));
    Log.info("RX LengthB %d",len);
    if (len < sizeof(rxbuf))
    {
        long_log_message((char * )rxbuf);
    }
    return ret;
}
//**********************
//Perform setup at start
//**********************
void setup() 
{
    Log.info("PROGRAM START");
    //*************************************
    //Set the callback for the RxTx service
    //Initialize variables
    //*************************************
    for (int i=0; i<NDEVICES; i++)
    {
        peerRxTxCharacteristic[i].onDataReceived(onDataReceived, &peerRxTxCharacteristic[i]);
        sSwitch[i]  = "unknown";
        sVoltage[i] = "unknown";
        sCurrent[i] = "unknown";
        sPower[i]   = "unknown";
        sPreSwitch[i] = "unknown";
        sBLEstatus[i] = "NotConn";
    }
    //******************************
    //Detect a disconnected BLE link
    //******************************
    BLE.onDisconnected(onDisconnect, NULL);
    //********************
    //Particle Cloud Setup
    //********************
    Particle.function("Control",control); //Define function access 
    //*************************************
    //Define accessible/viewable parameters
    //*************************************
    Particle.variable("Sw0state",   &sSwitch[0], STRING); 
    Particle.variable("Sw1state",   &sSwitch[1], STRING);
    Particle.variable("Sw0volts",   &sVoltage[0], STRING);
    Particle.variable("Sw1volts",   &sVoltage[1], STRING);
    Particle.variable("Sw0amps",    &sCurrent[0], STRING);
    Particle.variable("Sw1amps",    &sCurrent[1], STRING);
    Particle.variable("Sw0power",   &sPower[0], STRING);
    Particle.variable("Sw1power",   &sPower[1], STRING);
    Particle.variable("BLE0stat",   &sBLEstatus[0], STRING);
    Particle.variable("BLE1stat",   &sBLEstatus[1], STRING);
}
//*****************
//Background loop()
//*****************
#define POLL_PERIOD  3000UL //(3 second polling frequency)
void loop() 
{
    static int switch_index = 0;
    static unsigned long int last_service = 0;
    
    //**************
    //Pace ourselves
    //**************
    if ((millis() - last_service) >= POLL_PERIOD)
    {
        Log.info("Loop Entered - Switch %d",switch_index);
        last_service = millis();
        //******************************
        //Process an open BLE connection
        //******************************
    	if (peer[switch_index].connected())
    	{
    	    switch(state[switch_index])
    	    {
            case 0: //IDLE
                //**************************************
                //Read all the switch status information
                //**************************************
                WriteReadBLE(read_switch_status, switch_index); //Keep an eye on the switch
                //*********************
                //Parse out information
                //*********************
                sVoltage[switch_index]  = GetVoltage((char *)rxbuf);
                sPower[switch_index]    = GetPower((char *)rxbuf);
                sCurrent[switch_index]  = GetCurrent((char *)rxbuf);
                sSwitch[switch_index]   = GetSwitch((char *)rxbuf);
                //*********************************************
                //Check to see if external wall-switch was used
                //*********************************************
                if (sPreSwitch[switch_index] != "unknown" && sPreSwitch[switch_index] != sSwitch[switch_index])
                {
                    if (sSwitch[switch_index] == "ON")
                    {
                        Particle.publish("Shelly", sSwitchName[switch_index]+" WallSwitch ON", PRIVATE);
                    }
                    else
                    {
                        Particle.publish("Shelly", sSwitchName[switch_index]+" WallSwitch OFF", PRIVATE);
                    }
                }
                sPreSwitch[switch_index] = sSwitch[switch_index];
                break;
            case 1: //Operate a switch - ON
                Particle.publish("Shelly", sSwitchName[switch_index]+" Switch ON", PRIVATE);
                WriteReadBLE(write_switch_ON, switch_index); //Turn it ON
                state[switch_index] = 0; //Go back to "idle"
                sPreSwitch[switch_index] = "unknown";
                break;
            case 2: //Operate a switch - OFF
                Particle.publish("Shelly", sSwitchName[switch_index]+" Switch OFF", PRIVATE);
                WriteReadBLE(write_switch_OFF, switch_index); //Turn it OFF
                state[switch_index] = 0; //Go back to "idle"
                sPreSwitch[switch_index] = "unknown";
                break;
            case 3: //Read other info
                WriteReadBLE(read_device_info, switch_index);
                state[switch_index] = 0;
                break;
    	    }//end of switch
    	}
    	else
    	{
    	    //*************************************
    	    //Make the BLE connection to the switch
    	    //*************************************
            connectBLE(switch_index);
    	}
    
        //************************
        //Do one switch per loop()
        //************************
        if (++switch_index >= NDEVICES)
        {
            switch_index = 0;
        }
    }
}
//*************************************
//Make a BLE connection to a peripheral
//*************************************
void connectBLE(int sw)
{
    Log.info("BLEConnecting to switch%d!",sw);
	peer[sw] = BLE.connect(ShellyAddress[sw]);
    Log.info("BLE to switch%d!",sw);
	if (peer[sw].connected()) 
	{
        Log.info("successfully connected switch%d!",sw);
        peer[sw].getCharacteristicByUUID(peerTxCharacteristic[sw], uuidW);
        peer[sw].getCharacteristicByUUID(peerRxCharacteristic[sw], uuidRW);
        peer[sw].getCharacteristicByUUID(peerRxTxCharacteristic[sw], uuidREAD_NOTIFY);
        sBLEstatus[sw] = "CONNCTD";
    }
	else
	{
        Log.info("NOT connected (%d)!",sw);
        sBLEstatus[sw] = "NotConn";
        //delay(100);
        //System.reset(RESET_NO_WAIT); //Just like hitting the reset button
	}
}
//**********************************
//Function called via particle cloud
//**********************************
int control(String command) 
{
    int ret = -1; //Assume failure

    //****************************
    //Make sure command is "clean"
    //****************************
    command.toLowerCase();
    command.replace(" ", "");
    command.replace(".", "");
 
    Log.info("RXCMD->%s<-",command.c_str());
    //***************************************************
    //Validate the command.
    //The command is in the form of sw,state (eg. 0,1)
    //Where the first number is the switch index and 
    //the second number is the desired state (1=ON,0=OFF,?=QUERY)
    //If for any reason the command is notr recognized
    //then -1 is returned
    //***************************************************
    if (command.indexOf(",") != -1)
    {
        String ss = command.substring(0,command.indexOf(","));
        int sw = ss.toInt(); //Get switch number
        ss = command.substring(command.indexOf(",")+1); //grab desired state
        Log.info("COMMAND->%d,%s",sw,ss.c_str());
        ret = OperateSwitch(ss, sw); //Operate / query the switch
    }

    return ret;
}
//********************************************
//Parse message from cloud and act accordingly
//Using 0,1, and ? for later integration into
//homebridge.
//********************************************
int OperateSwitch(String command, int sw)
{
    int ret = -1; //Assume bad command
    //*************************
    //Are we turning on switch?
    //*************************
    if (sw < NDEVICES && peer[sw].connected())
    {
        if (command == "1")
        {
            //***********************************************************
            //Do this in background loop and not in this callback routine
            //***********************************************************
            state[sw] = 1; //Turn ON switch
            ret = 1;
        }
        else if (command == "0")
        {
            //***********************************************************
            //Do this in background loop and not in this callback routine
            //***********************************************************
            state[sw] = 2; //Turn OFF switch
            ret = 0;
        }
        else if (command=="?") //Query
        {
            //**********************
            //Return state of switch
            //**********************
            if (sSwitch[sw] == "true")
            {
                ret = 1;
            }
            else
            {
                ret = 0;
            }
        }
    }
    return ret;
}