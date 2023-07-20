#!/bin/sh 

git clone https://github.com/foss-xtensa/amazon-freertos.git FreeRTOS
cd FreeRTOS
git checkout 0909de2bd46fbb9d9f7297e09c17186275dc446c
cd portable/XCC/Xtensa
xt-make clean
xt-make
