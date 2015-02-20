#include "mbed.h"
#include "rtos.h"
#include "Terminal.h"

DigitalOut led1(LED1);          //asense
DigitalOut led2(LED2);          //vsense
DigitalOut led3(LED3);          //apace 
DigitalOut led4(LED4);          //vpace
DigitalOut aSense(p5);
DigitalOut vSense(p6);
InterruptIn APace(p7);
InterruptIn VPace(p8);

Terminal console(USBTX, USBRX);

int heartRate, avgHeartRate, observationInterval, rateCoefficient, observationRate, waitCount, senseWaitTime, testCase, changeModeTo;
int heartMode;                             //0 - Normal, 1 - Manual, 2 - Test
bool observationChange, digitOneReceived, paceReceived, synchDone, testOn, testResult[10]={false}, digitTwoReceived, changeMode, receivedVPace;
char ch;
char modeString[10];

const int nLRI=1500, nAVI = 60, nPVARP = 150, nURI = 600, nVRP = 100;   //timing constraints for Normal Mode of the Pacemaker
const int delta=10;

osMutexId displayMutex;
osMutexDef (displayMutex);
osMutexId observationChangeMutex;
osMutexDef(obserationChangeMutex);
osMutexId heartRateMutex;
osMutexDef (heartRateMuex);

Thread *SerialThreadPTR;
Thread *HeartSensePTR;
Thread *TestModePTR;
Thread *TestDisplayPTR;
RtosTimer *KeyTimeOutTimer;
RtosTimer *SenseWaitTimer;
//testCode
Timer t;

void resetDisplay()
{
    osMutexWait(displayMutex, osWaitForever);
    console.cls();
    console.locate(30, 6);
    console.printf("Heart Display");
    console.locate(30, 10);
    console.printf("Heart Rate :  %04d bpm", avgHeartRate);
    console.locate(30, 12);
    console.printf("Observation Interval :  %02d seconds", observationInterval/1000);
    console.locate(30, 14);
    console.printf("Mode :  %s", modeString);
    osMutexRelease(displayMutex);
}

void updateDisplay()
{
    osMutexWait(displayMutex, osWaitForever);
    console.locate(44, 10);
    console.printf("%04d", avgHeartRate);
    console.locate(54, 12);
    console.printf("%02d", observationInterval/1000);
    osMutexRelease(displayMutex);
}   


void heartSense(const void *args)
{
    while(1)
    {
        if(heartMode>0)
        {
            Thread::signal_wait(0x02);
        }
        //Thread::wait(500);              //chnage this
        senseWaitTime=50+(rand()%2000);
        SenseWaitTimer->start(senseWaitTime);
        Thread::signal_wait(0x03);
        if(rand()%2==0)
        {
            led1=1;
            aSense=1;
            Thread::wait(1);
            aSense=0;
            led1=0;
        }
        else
        {
            led2=1;
            vSense=1;
            Thread::wait(1);
            vSense=0;
            led2=0;
            osMutexWait(heartRateMutex, osWaitForever);
            heartRate++;
            osMutexRelease(heartRateMutex);
            
        }
    }
}

void display(const void *args)
{
    while(1)
    {
        Thread::wait(observationInterval);
        if(heartMode!=2)
        {
            waitCount++;
            if(!observationChange)
            {
                avgHeartRate=heartRate*rateCoefficient/waitCount;
                osMutexWait(heartRateMutex, osWaitForever);
                heartRate=0;
                osMutexRelease(heartRateMutex);
                if(observationRate!=(observationInterval/1000))
                {
                    resetDisplay();
                    //update observationrate after the current interval stats  are displayed
                    observationInterval=observationRate*1000;           
                    rateCoefficient=(60000/observationInterval);
                } 
                else
                {
                    updateDisplay();
                }
                waitCount=0;   
            }        
        }
    }
}

void senseWait(const void *args)
{
    (*HeartSensePTR).signal_set(0x03);
}

void keyTimeOut(const void *args)
{
    if(digitOneReceived)
    {
        observationChange=false;
    }
    else
    {
        observationChange=false;
    }
    resetDisplay();
}

void testMode(const void *args)
{
    while(1)
    {
        if(heartMode!=2)
        {
            Thread::signal_wait(0x04);  
        }
        else
        {
            osMutexWait(displayMutex, osWaitForever);
            console.cls();
            osMutexRelease(displayMutex);
            //synch with PM
            //TestCase 1 - Asense at PVARP+; No Vpace within URI-PVARP (detected within URI-PVARP+delta) 
            testCase=0;
            testResult[testCase]=true;
            osMutexWait(displayMutex, osWaitForever);
            console.locate(20,testCase);
            osMutexRelease(displayMutex);
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            t.reset();
            t.start();
            Thread::wait(nPVARP);
            led1=1;
            aSense=1;
            Thread::wait(1);
            aSense=0;
            led1=0;
            osMutexWait(displayMutex, osWaitForever);
            console.printf("AS : %d      ",t.read_ms());
            osMutexRelease(displayMutex);
            testOn=true;
            Thread::wait(nURI-nPVARP-delta);
            testOn=false;
            if(testResult[testCase])
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d pass",testCase+1);
                osMutexRelease(displayMutex);
            }
            else
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d fail",testCase+1);
                osMutexRelease(displayMutex);                
            }
            
            //synch with PM
            //TestCase 2 - Asense at URI+; Vpace within AVI+delta
            testCase=1;
            osMutexWait(displayMutex, osWaitForever);
            console.locate(20,testCase);
            osMutexRelease(displayMutex);
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            t.reset();
            Thread::wait(nURI);
            led1=1;
            aSense=1;
            Thread::wait(1);
            aSense=0;
            led1=0;
            osMutexWait(displayMutex, osWaitForever);
            console.printf("AS : %d      ",t.read_ms());
            osMutexRelease(displayMutex);
            testOn=true;
            Thread::wait(nAVI+delta);
            testOn=false;
            if(testResult[testCase])
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d pass",testCase+1);
                osMutexRelease(displayMutex);
            }
            else
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d fail",testCase+1);
                osMutexRelease(displayMutex);                
            }     
            //synch with PM
            //TestCase 3 - Asense at URI+; Vsense at AVI-; No pace detected; run this 4 times
            testCase=2;
            testResult[testCase]=true;
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            t.reset();
            Thread::wait(nURI);
            testOn=true;
            for(int i=0;i<5;i++)
            {
                /*osMutexWait(displayMutex, osWaitForever);
                console.locate(20,testCase);
                osMutexRelease(displayMutex);*/
                led1=1;
                aSense=1;
                Thread::wait(1);
                aSense=0;
                led1=0;
                /*osMutexWait(displayMutex, osWaitForever);
                console.printf("AS : %d      ",t.read_ms());
                osMutexRelease(displayMutex);*/
                Thread::wait(nAVI-delta);
                led2=1;
                vSense=1;
                Thread::wait(1);
                vSense=0;
                led2=0;  
                /*osMutexWait(displayMutex, osWaitForever);
                console.printf("VS : %d      ",t.read_ms());
                osMutexRelease(displayMutex);*/
                Thread::wait(nLRI-nAVI-delta);                             
            }
            testOn=false;
            if(testResult[testCase])
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d pass",testCase+1);
                osMutexRelease(displayMutex);
            }
            else
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d fail",testCase+1);
                osMutexRelease(displayMutex);                
            }
            //synch with PM
            //TestCase 4 - No sensing;Only pacing;
            testCase=3;
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            t.reset();
            testOn=true;
            for(int i=0;i<5;i++)
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(20,testCase);
                osMutexRelease(displayMutex);
                t.reset();
                testResult[testCase]=false;
                Thread::wait(nLRI-nAVI);
                if(!testResult[testCase])
                {
                    break;
                }
                testResult[testCase]=false;           
                Thread::wait(nAVI-delta);    
                if(!testResult[testCase])
                {
                    break;
                }
            }
            testOn=false;
            if(testResult[testCase])
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d pass",testCase+1);
                osMutexRelease(displayMutex);
            }
            else
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d fail",testCase+1);
                osMutexRelease(displayMutex);                
            }

            //synch with PM
            //TestCase 5 - Vsense at VRP+; APace at LRI-AVI+delta
            testCase=4;
            testResult[testCase]=false;
            osMutexWait(displayMutex, osWaitForever);
            console.locate(20,testCase);
            osMutexRelease(displayMutex);
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            t.reset();
            Thread::wait(nPVARP);
            led2=1;
            vSense=1;
            Thread::wait(1);
            vSense=0;
            led2=0;
            osMutexWait(displayMutex, osWaitForever);
            console.printf("VS : %d      ",t.read_ms());
            osMutexRelease(displayMutex);
            testOn=true;
            Thread::wait(nLRI-nAVI+delta);
            testOn=false;
            if(testResult[testCase])
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d pass",testCase+1);
                osMutexRelease(displayMutex);
            }
            else
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d fail",testCase+1);
                osMutexRelease(displayMutex);                
            }
            
            //synch with PM
            //TestCase 6 - Asense at VRP+ (within PVARP); A pace should come at LRI-AVI+delta and no VPace should come
            testCase=5;
            testResult[testCase]=false;
            osMutexWait(displayMutex, osWaitForever);
            console.locate(20,testCase);
            osMutexRelease(displayMutex);
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            t.reset();
            Thread::wait(nVRP);
            led1=1;
            aSense=1;
            Thread::wait(1);
            aSense=0;
            led1=0;
            osMutexWait(displayMutex, osWaitForever);
            console.printf("AS : %d      ",t.read_ms());
            osMutexRelease(displayMutex);
            testOn=true;
            Thread::wait(nLRI-nAVI-nVRP);
            testOn=false;
            if(testResult[testCase])
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d pass",testCase+1);
                osMutexRelease(displayMutex);
            }
            else
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d fail",testCase+1);
                osMutexRelease(displayMutex);                
            }
            
            //synch with PM
            //TestCase 7 - Vsense at VRP-; Asense should come at LRI-AVI+delta; Asense does not come after that, which would happen if Vsense was accepted
            testCase=6;
            testResult[testCase]=false;
            osMutexWait(displayMutex, osWaitForever);
            console.locate(20,testCase);
            osMutexRelease(displayMutex);
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            t.reset();
            Thread::wait(nVRP-delta-10);            //give vSense within VRP
            led2=1;
            vSense=1;
            Thread::wait(1);
            vSense=0;
            led2=0;
            osMutexWait(displayMutex, osWaitForever);
            console.printf("VS : %d      ",t.read_ms());
            osMutexRelease(displayMutex);
            testOn=true;
            Thread::wait(nLRI-nAVI-nVRP+delta+10);
            testOn=false;
            if(testResult[testCase])
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d pass",testCase+1);
                osMutexRelease(displayMutex);
            }
            else
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(0,testCase);
                console.printf("test %d fail",testCase+1);
                osMutexRelease(displayMutex);                
            }
            t.stop();
            //synch with PM
            //TestCase 8 - Alarm High case; generate Asense and Vsense at very high rate
            testCase=7;
            testResult[testCase]=false;
            osMutexWait(displayMutex, osWaitForever);
            console.locate(0,testCase);
            console.printf("Alarm High");
            osMutexRelease(displayMutex);
            synchDone=false;
            Thread::signal_wait(0x05);
            synchDone=true;
            for(int i=0;i<5000;i++)
            {
                led1=1;
                aSense=1;
                Thread::wait(1);
                aSense=0;
                led1=0;
                led2=1;
                vSense=1;
                Thread::wait(1);
                vSense=0;
                led2=0;                            
            }
            osMutexWait(displayMutex, osWaitForever);
            console.locate(0,testCase+1);
            console.printf("Testing Complete");
            osMutexRelease(displayMutex);
            
        }
        if(changeMode)
        {
            changeMode=false;
            if(changeModeTo==0)
            {
                heartMode=0;
                strcpy(modeString,"Random");
                osMutexWait(displayMutex, osWaitForever);
                console.cls();
                console.locate(30, 10);
                console.printf("Heart Rate :  %03d bpm", avgHeartRate);
                console.locate(30, 12);
                console.printf("Observation Interval :  %02d seconds", observationInterval/1000);
                console.locate(30, 14);
                console.printf("Mode :  %s", modeString);
                osMutexRelease(displayMutex);
                (*HeartSensePTR).signal_set(0x02);
            }
            else
            {
                heartMode=1;
                strcpy(modeString,"Manual");
                osMutexWait(displayMutex, osWaitForever);
                console.cls();
                console.locate(30, 10);
                console.printf("Heart Rate :  %03d bpm", avgHeartRate);
                console.locate(30, 12);
                console.printf("Observation Interval :  %02d seconds", observationInterval/1000);
                console.locate(30, 14);
                console.printf("Mode :  %s", modeString);
                osMutexRelease(displayMutex);
            }
            
        }
    }
}

void aPace()
{
    if(heartMode==0)
    {
        SenseWaitTimer->start(50+(rand()%2000));         //TODO move this
    }
    led3=1;
    wait(0.01);
    led3=0;
    if(heartMode==2 && testOn)
    {
         receivedVPace=false;
        (*TestDisplayPTR).signal_set(0x06);
        if(testCase==2)
        {
            testResult[testCase]=false;            
        }
        else if(testCase==3 || testCase==4 || testCase==5 || testCase==6)
        {
             testResult[testCase]=true;   
        }
    }
}

void vPace()
{
    if(heartMode==0)
    {
        SenseWaitTimer->start(50+(rand()%2000));         //TODO move this
    }
    led4=1;
    wait(0.01);
    led4=0;
    osMutexWait(heartRateMutex, osWaitForever);
    heartRate++;
    osMutexRelease(heartRateMutex);
    if(heartMode==2 && !synchDone)
    {
        (*TestModePTR).signal_set(0x05);
    }
    if(heartMode==2 && testOn)
    {
        receivedVPace=true;
        (*TestDisplayPTR).signal_set(0x06);
        if(testCase==0 || testCase==2 || testCase==5 || testCase==6)
        {
            testResult[testCase]=false;            
        }
        else if(testCase==1 || testCase==3)
        {
            testResult[testCase]=true;
        }
    }
}

void testDisplay(const void *args)
{
    while(1)
    {
        Thread::signal_wait(0x06);
        if(receivedVPace)
        {
            osMutexWait(displayMutex, osWaitForever);
            console.printf("VP : %d      ",t.read_ms());
            osMutexRelease(displayMutex);
        }
        else
        {
            osMutexWait(displayMutex, osWaitForever);
            console.printf("AP : %d      ",t.read_ms());
            osMutexRelease(displayMutex);           
        }
    }
}
        

void serialThread(const void *args)
{
    while(1)
    {
        Thread::signal_wait(0x01);
        if((((ch=='a')||(ch=='A')) && heartMode==1) && !observationChange)
        {
            //fire A Signal
            led1=1;
            aSense=1;
            Thread::wait(1);
            led1=0;
            aSense=0;
        }
        else if((((ch=='v')||(ch=='V')) && heartMode==1) && !observationChange)
        {
            //fire V Signal
            led2=1;
            vSense=1;
            Thread::wait(1);
            vSense=0;
            led2=0;
            osMutexWait(heartRateMutex, osWaitForever);
            heartRate++;
            osMutexRelease(heartRateMutex);
        }
        else if(((((ch=='r')||(ch=='R'))&& heartMode!=0) && !observationChange) && !changeMode)
        {
            if(heartMode==2)
            {
                changeMode=true;
                changeModeTo=0;
                console.locate(30, 14);
                console.printf("Mode :  %s (Pending - Random)", modeString);                
            }
            else
            {                
                heartMode=0;
                strcpy(modeString,"Random");
                resetDisplay();
                (*HeartSensePTR).signal_set(0x02);
            }
        }
        else if(((((ch=='m')||(ch=='M')) && heartMode!=1) && !observationChange) && !changeMode)
        {
            if(heartMode==2)
            {
                changeMode=true;
                changeModeTo=1;
                console.locate(30, 14);
                console.printf("Mode :  %s (Pending - Manual)", modeString);
            }
            else
            {
                heartMode=1;
                strcpy(modeString,"Manual");
                resetDisplay();
            }
        }
        else if((((ch=='t')||(ch=='T')) && heartMode!=2) && !observationChange)
        {
            heartMode=2;            //spawn Test Thread
            strcpy(modeString,"Test");
            resetDisplay();
            (*TestModePTR).signal_set(0x04);
        }
        else if((((ch=='o')||(ch=='O')) && !observationChange) && heartMode!=2)
        {
            observationChange=true;
            digitOneReceived=false;
            digitTwoReceived=false;
            //spawn a timer for  3 seconds
            osMutexWait(displayMutex, osWaitForever);
            console.locate(29, 16);
            console.printf("Observation Interval change : -- seconds");
            osMutexRelease(displayMutex);
            KeyTimeOutTimer=new RtosTimer(keyTimeOut, osTimerOnce, (void *)0);
            KeyTimeOutTimer->start(3000);
        }
        else if((observationChange) && ((ch>=48) && (ch<=57)) && !digitTwoReceived)
        {
            if(!digitOneReceived)
            {
                KeyTimeOutTimer->start(3000);                       //key time out is 3 seconds
                osMutexWait(observationChangeMutex, osWaitForever);         //mutex to be released either on time out or if 2 digits are received
                observationRate=ch-'0';
                osMutexRelease(observationChangeMutex);
                digitOneReceived=true;
                osMutexWait(displayMutex, osWaitForever);
                console.locate(60, 16);
                console.printf("%02d", observationRate);
                osMutexRelease(displayMutex);
            }
            else
            {
                KeyTimeOutTimer->stop();
                digitTwoReceived=true;
                osMutexWait(observationChangeMutex, osWaitForever);
                observationRate=(observationRate*10)+(ch-'0');
                osMutexRelease(observationChangeMutex);
                observationChange=false;
                osMutexWait(displayMutex, osWaitForever);
                console.locate(60, 16);
                console.printf("%02d", observationRate);
                osMutexRelease(displayMutex);
            }
        }                
    }
}

// Interupt Routine to read in data from serial port and call the serial thread
void Rx_interrupt() 
{
    if(console.readable())
    {
        ch=LPC_UART0->RBR;                  //read uart buffer data and clear the interrupt flag
        (*SerialThreadPTR).signal_set(0x01);     //initiate the serial thread to change the state of the timer
    }
    
}


int main() 
{
    console.attach(&Rx_interrupt, Serial::RxIrq);        //attach a uart interrupt for uart
    console.cls();
    console.locate(30, 6);
    console.printf("Heart Display");
    console.locate(30, 10);
    console.printf("Heart Rate :  ---- bpm");
    console.locate(30, 12);
    console.printf("Observation Interval :  -- seconds");
    console.locate(30, 14);
    console.printf("Mode :  Random ");
    
    //initialize variables
    heartMode=0;
    heartRate=0;
    avgHeartRate=0;
    waitCount=0;
    observationInterval=5000;
    rateCoefficient=(60000/observationInterval);
    observationChange=false;
    observationRate=5;
    digitOneReceived=false;
    strcpy(modeString,"Random");
    synchDone=true;
    testCase=0;
    testOn=false;
    digitTwoReceived=false;
    changeMode=false;
    changeModeTo=0;
    receivedVPace=false;
    
    SenseWaitTimer=new RtosTimer(senseWait, osTimerOnce, (void *)0);
    Thread SerialThread(serialThread);
    SerialThreadPTR=&SerialThread;
    SerialThread.set_priority(osPriorityRealtime);
    Thread HeartSense(heartSense);
    HeartSensePTR=&HeartSense;
    HeartSense.set_priority(osPriorityHigh);
    Thread Display(display);
    Display.set_priority(osPriorityAboveNormal);
    Thread TestMode(testMode);
    TestModePTR=&TestMode;
    TestMode.set_priority(osPriorityAboveNormal);
    Thread TestDisplay(testDisplay);
    TestDisplayPTR=&TestDisplay;
    TestDisplay.set_priority(osPriorityHigh);
    APace.fall(&aPace);
    VPace.fall(&vPace);
    
    while(1);
}
