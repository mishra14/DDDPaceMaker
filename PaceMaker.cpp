#include "mbed.h"
#include "rtos.h"
#include "Terminal.h"

#define LRI 0
#define AVI 1
#define VRP 2
#define PVARP 3
#define URI 4
#define PAVB 5
#define VSP 6
#define NR -1               //not running 

DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalOut led4(LED4);
DigitalOut buzzer(p9);
DigitalOut aPace(p7);
DigitalOut vPace(p8);
InterruptIn ASignal(p5);
InterruptIn VSignal(p6);
Terminal console(USBTX, USBRX);

bool expectingASignal, expectingVSignal, paceA, observationChange, digitOneReceived, modeChanged, canPaceV, paceVPending, ringAlarm, ringingAlarm, timerRunning, aSenseOccurred, digitTwoReceived;
int timeOutValue[7];              //timeout array that holds values for LRI, VRP, PVARP, AVI, URI, PAVB, VSP; PVARP >VRP
int heartRate, observationInterval, observationRate, timeOutStatus, waitCount, avgHeartRate, rateCoefficient, heartRateHeart, sec, avgHeartRateHeart;
int paceMakerMode=1;            //1 - Normal, 2 - Exercise, 3 - Sleep, 4 - Manual
int uriTimeOutStatus=URI;
char ch;
char modeString[20];
const int nLRI=1500, nAVI = 60, nPVARP = 150, nURI = 600, nVRP = 100, nVSP = 0, nPAVB = 20;
const int sLRI=2000, sAVI = 60, sPVARP = 150, sURI = 1000, sVRP = 100, sVSP = 0, sPAVB = 20;
const int eLRI=1000, eAVI = 60, ePVARP = 150, eURI = 400, eVRP = 100, eVSP = 0, ePAVB = 20;

osMutexId displayMutex;
osMutexDef (displayMutex);
osMutexId observationChangeMutex;
osMutexDef (obserationChangeMutex);
osMutexId expectAMutex;
osMutexDef (expectAMutex);
osMutexId expectVMutex;
osMutexDef (expectVMutex);
osMutexId timeOutStatusMutex;
osMutexDef (timeOutStatusMutex);
osMutexId heartRateMutex;
osMutexDef (heartRateMutex);

Thread *SerialThreadPTR;
Thread *PacePTR;
Thread *ModeChangePTR;
Thread *PMSensePTR;
RtosTimer *TimeOutTimer;
RtosTimer *URITimeOutTimer;
RtosTimer *KeyTimeOutTimer;
RtosTimer *SecondsTimer;


void setTimeOutValues(int tLRI, int tAVI, int tPVARP, int tURI, int tVRP, int tVSP, int tPAVB)
{
    timeOutValue[LRI]=tLRI;
    timeOutValue[AVI]=tAVI;
    timeOutValue[PVARP]=tPVARP;
    timeOutValue[URI]=tURI;
    timeOutValue[VRP]=tVRP;
    timeOutValue[VSP]=tVSP;
    timeOutValue[PAVB]=tPAVB;
}

void resetDisplay()
{
    osMutexWait(displayMutex, osWaitForever);
    console.cls();
    console.locate(30, 4);
    console.printf("Pace Maker Display");
    console.locate(30, 8);
    console.printf("Heart Rate :  %04d bpm", avgHeartRate);
    console.locate(30, 10);
    console.printf("Observation Interval :  %02d seconds", observationInterval/1000);
    console.locate(30, 12);
    console.printf("Mode :  %s", modeString);
    console.locate(30, 14);
    console.printf("Heart Beat Rate :  %04d  bpm", (heartRateHeart*(60/sec)));    
    osMutexRelease(displayMutex);
    ringingAlarm=false;
}

void updateDisplay()
{
    osMutexWait(displayMutex, osWaitForever);
    console.locate(44, 8);
    console.printf("%04d", avgHeartRate);
    console.locate(54, 10);
    console.printf("%02d", observationInterval/1000);
    console.locate(49, 14);
    console.printf("%04d", (heartRateHeart*(60/sec))); 
    osMutexRelease(displayMutex);
}   

void changeMode()
{
    switch(paceMakerMode)
    {
        case 1:             //Normal Mode
        {
            setTimeOutValues(nLRI, nAVI, nPVARP, nURI, nVRP, nVSP, nPAVB);
            strcpy(modeString, "Normal");
            break;
        }
        case 2:             //Exercise Mode
        {
            setTimeOutValues(eLRI, eAVI, ePVARP, eURI, eVRP, eVSP, ePAVB);
            strcpy(modeString, "Exercise");
            break;
        }
        case 3:             //Sleep Mode
        {
            setTimeOutValues(sLRI, sAVI, sPVARP, sURI, sVRP, sVSP, sPAVB);
            strcpy(modeString, "Sleep");
            break;
        }
        case 4:             //Manual Mode; do not chnage the time out values
        {
            strcat(modeString, " + Manual");
            break;
        }
        default:            //Normal mode is any error occurs
        {
            setTimeOutValues(nLRI, nAVI, nPVARP, nURI, nVRP, nVSP, nPAVB);
            strcpy(modeString, "Normal");
        }
    }    
    modeChanged=false;
    resetDisplay();
}

void modeChange(const void *args)
{
    while(1)
    {
        Thread::signal_wait(0x03);
        if(modeChanged)
        {
            changeMode();
        }
    }
}
            

void aSense()
{
    //ASignal received from heart  
    if(expectingASignal)
    {
        led4=1;
        wait(0.001);
        led4=0; 
        if(modeChanged)
        {
            (*ModeChangePTR).signal_set(0x03);
        }
        aSenseOccurred=true;
        (*PMSensePTR).signal_set(0x04);
    }    
}

void vSense()
{
    //VSignal received from heart
    //reset timeout
    //increment heart rate
    heartRateHeart++;
    if(expectingVSignal)
    {
        led3=1;
        wait(0.001);
        led3=0;
        if(modeChanged)
        {
            (*ModeChangePTR).signal_set(0x03);
        }
        canPaceV=false;
        aSenseOccurred=false;
        (*PMSensePTR).signal_set(0x04);
    }
    
}

void seconds(const void *args)
{
    sec++;
    if(sec>=60)
    {
        avgHeartRateHeart=heartRateHeart;
        heartRateHeart=0;
        sec=0;
    }
}

void timeOut(const void *args)
{
    // check which time out has occurred
    // generate appropriate pace signal
    // reset timer to new value  
    //led2=1; 
    if(timeOutStatus==AVI)
    {
        //generate VPace
        if(canPaceV)
        {
            paceA=false;
            (*PacePTR).signal_set(0x01);
        }
        else
        {
            canPaceV=true;
        }
    }
    else if(timeOutStatus==PAVB)
    {
        osMutexWait(expectVMutex,osWaitForever);
        expectingVSignal=true;
        osMutexRelease(expectVMutex);
        osMutexWait(timeOutStatusMutex, osWaitForever);
        timeOutStatus=AVI;
        osMutexRelease(timeOutStatusMutex);
        timerRunning=true;
        TimeOutTimer->start(timeOutValue[AVI]-timeOutValue[PAVB]);
    }
    else if(timeOutStatus==VRP)
    {
        //now we can sense a Ventrival event, but not an atrial event as PVARP is not over 
        //restart timer for PVARP
        osMutexWait(expectVMutex,osWaitForever);        
        expectingVSignal=true;         
        osMutexRelease(expectVMutex);
        osMutexWait(timeOutStatusMutex, osWaitForever);
        timeOutStatus=PVARP;
        osMutexRelease(timeOutStatusMutex);
        timerRunning=true;
        TimeOutTimer->start(timeOutValue[PVARP]-timeOutValue[VRP]);
    }
    else if(timeOutStatus==PVARP)
    {
        //now we can sense Atrial events as well
        osMutexWait(expectAMutex,osWaitForever);
        expectingASignal=true;
        osMutexRelease(expectAMutex);
        osMutexWait(timeOutStatusMutex, osWaitForever);
        timeOutStatus=LRI;
        osMutexRelease(timeOutStatusMutex);
        timerRunning=true;
        TimeOutTimer->start(timeOutValue[LRI]-timeOutValue[PVARP]-timeOutValue[AVI]);
    }
    else if(timeOutStatus==LRI)
    {
        //generate APace
        paceA=true;
        (*PacePTR).signal_set(0x01);
    }
}

void uriTimeOut(const void *args)
{
    // uri is over 
    //check is a vpace has to be generated; If yes then generate the pace; else enable a flag that lets the thread generate the pace
    if(paceVPending || canPaceV)
    {
        //pace V as a pace occurred during URI
        paceA=false;
        paceVPending=false;
        (*PacePTR).signal_set(0x01);
    }
    else
    {
        canPaceV=true;              //allow the PM to pace V as URI is now over
    }
    
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

void pmSense(const void *args)
{
    while(1)
    {
        Thread::signal_wait(0x04);
        if(timerRunning)
        {
            TimeOutTimer->stop();
            timerRunning=false;
        }
        if(aSenseOccurred)
        {
            osMutexWait(expectAMutex,osWaitForever);
            expectingASignal=false;
            osMutexRelease(expectAMutex);
            osMutexWait(expectVMutex,osWaitForever);
            expectingVSignal=true;
            osMutexRelease(expectVMutex);
            timerRunning=true;
            osMutexWait(timeOutStatusMutex, osWaitForever);
            timeOutStatus=AVI;
            osMutexRelease(timeOutStatusMutex);
            TimeOutTimer->start(timeOutValue[AVI]);//500);            
        }
        else
        {
            osMutexWait(heartRateMutex, osWaitForever);
            heartRate++;
            osMutexRelease(heartRateMutex);
            osMutexWait(expectVMutex,osWaitForever);         
            expectingVSignal=false;         
            osMutexRelease(expectVMutex);
            osMutexWait(expectAMutex,osWaitForever);
            expectingASignal=false;
            osMutexRelease(expectAMutex);
            canPaceV=false;
            URITimeOutTimer->start(timeOutValue[URI]);
            timerRunning=true;
            osMutexWait(timeOutStatusMutex, osWaitForever);
            timeOutStatus=VRP;
            osMutexRelease(timeOutStatusMutex);
            TimeOutTimer->start(timeOutValue[VRP]);
        }
    }
}
        

void pace(const void *args)
{
    while(1)
    {
        Thread::signal_wait(0x01);
        if(paceA)
        {
            led2=1;
            aPace=1;
            Thread::wait(1);
            aPace=0;
            led2=0;
            if(modeChanged)
            {
                changeMode();
            }
            // start AVI Timer
            osMutexWait(expectAMutex,osWaitForever);
            expectingASignal=false;
            osMutexRelease(expectAMutex);
            osMutexWait(expectVMutex,osWaitForever);         
            expectingVSignal=false;         
            osMutexRelease(expectVMutex);
            osMutexWait(timeOutStatusMutex, osWaitForever);
            timeOutStatus=PAVB;
            osMutexRelease(timeOutStatusMutex);
            timerRunning=true;
            TimeOutTimer->start(timeOutValue[PAVB]);
            //generate the APace pulse
        }
        else
        {
            led1=1;
            vPace=1;
            Thread::wait(1);
            vPace=0;
            led1=0;
            if(modeChanged)
            {
                changeMode();
            }
            // start VRP and URI timers
            osMutexWait(expectVMutex,osWaitForever);         
            expectingVSignal=false;         
            osMutexRelease(expectVMutex);
            osMutexWait(expectAMutex,osWaitForever);
            expectingASignal=false;
            osMutexRelease(expectAMutex);
            osMutexWait(heartRateMutex, osWaitForever);
            heartRate++;
            osMutexRelease(heartRateMutex);
            canPaceV=false;
            URITimeOutTimer->start(timeOutValue[URI]);
            osMutexWait(timeOutStatusMutex, osWaitForever);
            timeOutStatus=VRP;
            osMutexRelease(timeOutStatusMutex);
            timerRunning=true;
            TimeOutTimer->start(timeOutValue[VRP]);
            //generate the VPace pulse
        }
    }
}

void serialThread(const void *args)
{
    while(1)
    {
        Thread::signal_wait(0x02);
        if((((ch=='a')||(ch=='A')) && paceMakerMode==4) && !observationChange)
        {
            //fire A Pace
            paceA=true;
            (*PacePTR).signal_set(0x01);
        }
        else if((((ch=='v')||(ch=='V')) && paceMakerMode==4) && !observationChange)
        {
            //fire V Pace
            if(canPaceV)
            {
                paceA=false;
                paceVPending=false;
                (*PacePTR).signal_set(0x01);
            }
            else
            {
                paceVPending=true;
            }
        }
        else if(((((ch=='n')||(ch=='N'))&& paceMakerMode!=1) && !observationChange) && !modeChanged)
        {
            paceMakerMode=1;                //change Mode to Normal
            modeChanged=true;
        }
        else if(((((ch=='e')||(ch=='E')) && paceMakerMode!=2) && !observationChange) && !modeChanged)
        {
            paceMakerMode=2;                //chnage mode to Exercise
            modeChanged=true;
        }
        else if(((((ch=='s')||(ch=='S')) && paceMakerMode!=3) && !observationChange) && !modeChanged)
        {
            paceMakerMode=3;            //change mode to Sleep
            modeChanged=true;
        }
        else if(((((ch=='b')||(ch=='B')) ) && !observationChange) )
        {
            ringAlarm=!ringAlarm;
        }
        else if(((((ch=='m')||(ch=='M')) && paceMakerMode!=4) && !observationChange) && !modeChanged)
        {
            paceMakerMode=4;            //change mode to Sleep
            modeChanged=true;
        }
        else if(((((ch=='o')||(ch=='O')) && !observationChange) && !modeChanged))
        {
            observationChange=true;
            digitOneReceived=false;
            digitTwoReceived=false;
            //spawn a timer for  3 seconds
            osMutexWait(displayMutex, osWaitForever);
            console.locate(30, 18);
            console.printf("Observation Interval change : -- seconds");
            osMutexRelease(displayMutex);
            KeyTimeOutTimer=new RtosTimer(keyTimeOut, osTimerOnce, (void *)0);
            KeyTimeOutTimer->start(3000);
        }
        else if((observationChange) && ((ch>=48) && (ch<=57)))
        {
            if(!digitOneReceived)
            {
                KeyTimeOutTimer->start(3000);
                osMutexWait(observationChangeMutex, osWaitForever);
                observationRate=ch-'0';
                osMutexRelease(observationChangeMutex);
                digitOneReceived=true;
                osMutexWait(displayMutex, osWaitForever);
                console.locate(60, 18);
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
                console.locate(60, 18);
                console.printf("%02d", observationRate);
                osMutexRelease(displayMutex);
            }
        }                
    }
}

void alarm(const void *args)
{
    while(1)
    {
        Thread::wait(1000);
        while(((heartRateHeart*(60/sec))>(60000/timeOutValue[URI])) && ringAlarm)
        {
            buzzer=1;
            Thread::wait(5);
            buzzer=0;
            Thread::wait(5);
            if(!ringingAlarm)
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(30, 16);
                console.printf("Alarm : HeartRate too high");
                osMutexRelease(displayMutex);
                ringingAlarm=true;
            }
        }
        while(((heartRateHeart*(60/sec))<(60000/timeOutValue[LRI])) && ringAlarm)
        {
            buzzer=1;
            Thread::wait(10);
            buzzer=0;
            Thread::wait(10);
            if(!ringingAlarm)
            {
                osMutexWait(displayMutex, osWaitForever);
                console.locate(30, 16);
                console.printf("Alarm : HeartRate too Low");
                osMutexRelease(displayMutex);
                ringingAlarm=true;
            }
        }
        if(ringingAlarm)
        {
            ringingAlarm=false;
            resetDisplay();
        }
    }
}

void display(const void *args)
{
    while(1)
    {
        Thread::wait(observationInterval);
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


void Rx_interrupt()
{
    if(console.readable())
    {
        ch=LPC_UART0->RBR;                  //read uart buffer data and clear the interrupt flag
        (*SerialThreadPTR).signal_set(0x02);     //initiate the serial thread to change the state of the timer
    }
}

int main() 
{
    console.attach(&Rx_interrupt, Serial::RxIrq);        //attach a uart interrupt for uart
    console.cls();
    console.locate(30, 4);
    console.printf("Pace Maker Display");
    console.locate(30, 8);
    console.printf("Heart Rate :  --- bpm");
    console.locate(30, 10);
    console.printf("Observation Interval :  -- seconds");
    console.locate(30, 12);
    console.printf("Mode :  Normal");
    console.locate(30, 14);
    console.printf("Heart Beat Rate :  ----  bpm");   
    
    //initialize variables
    osMutexWait(expectAMutex,osWaitForever);
    expectingASignal=true;
    osMutexRelease(expectAMutex);
    osMutexWait(expectVMutex,osWaitForever);         
    expectingVSignal=true;         
    osMutexRelease(expectVMutex);
    setTimeOutValues(nLRI, nAVI, nPVARP, nURI, nVRP, nVSP, nPAVB);
    heartRate=0;
    avgHeartRate=0;
    paceMakerMode=1;
    observationInterval=5000;
    observationRate=5;
    waitCount=0;
    rateCoefficient=12;
    paceA=false;
    observationChange=false;
    digitOneReceived=false;
    digitTwoReceived=false;
    modeChanged=false;
    canPaceV=true;                              //assume at begining that URI has lapsed and start with an atrial event
    paceVPending=false;                                //assume that a VPace request has not happened already
    strcpy(modeString, "Normal");
    buzzer=0;
    ringAlarm=true;
    heartRateHeart=0;
    sec=0;
    avgHeartRateHeart=0;
    ringingAlarm=false;
    timerRunning=false;
    aSenseOccurred=true;
    
    ASignal.fall(&aSense);
    VSignal.fall(&vSense);
    TimeOutTimer=new RtosTimer(timeOut, osTimerOnce, (void*)0);
    URITimeOutTimer=new RtosTimer(uriTimeOut, osTimerOnce, (void *)0);
    SecondsTimer=new RtosTimer(seconds, osTimerPeriodic, (void *)0);                //timer that over runs every 1 second and is used to reset the heart rate count coming from the heart
    SecondsTimer->start(1000);                                                      //start the timer to run for every 1 second
    
    Thread Pace(pace);
    PacePTR=&Pace;
    Pace.set_priority(osPriorityHigh);
    Thread PMSense(pmSense);
    PMSensePTR=&PMSense;
    PMSense.set_priority(osPriorityAboveNormal);
    Thread Alarm(alarm);
    Alarm.set_priority(osPriorityAboveNormal);
    Thread ModeChange(modeChange);
    ModeChangePTR=&ModeChange;
    ModeChange.set_priority(osPriorityAboveNormal);
    Thread Display(display);
    Thread SerialThread(serialThread);
    SerialThreadPTR=&SerialThread;
    SerialThread.set_priority(osPriorityRealtime);
    Display.set_priority(osPriorityAboveNormal);
    
    osMutexWait(timeOutStatusMutex, osWaitForever);
    timeOutStatus=VRP;
    osMutexRelease(timeOutStatusMutex);
    //test lines to start pacing without heart; heart starts immediately after an Atrial event
    //timeOutStatus=AVI;
    TimeOutTimer->start(timeOutValue[VRP]);
    while(1);
}
