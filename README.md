# DDDPaceMaker
Cardiac Pacemaker (Fall 2014) - Designed an UPPAAL model and developed a system using LPC 1768 and CMSIS RTX RTOS to implement a cardiac pacemaker, in normal DDD mode to prevent bradycardia, with a time assurance of less than 5 msec delay.

Platform - mBed (NXP LPC1768 Microcontroller / ARM Cortex M3 )
OS - mBedRTOS (CMSIS RTX port for mBed)
languages - C and xml

Files - 

Heart.cpp - C code for emulating a random beating heart and testing the timings for the pacemaker
PaceMaker.cpp - C code for emulating a pacemaker.
BasicodelFinal and FullModelFinal - XML files containing UPPAAL models for model checkig and verification.
541_project_presentation_group6.pptx - Power point explaining the basic operation and features
