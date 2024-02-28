#!/bin/sh 

git clone https://github.com/foss-xtensa/amazon-freertos.git FreeRTOS
cd FreeRTOS
git checkout 2ca806e59d8eb2490726f2ce1460f256ab593cf3 
cd portable/XCC/Xtensa
xt-make clean
xt-make
