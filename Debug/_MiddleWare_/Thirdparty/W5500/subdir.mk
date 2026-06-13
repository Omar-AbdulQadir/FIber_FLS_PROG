################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../_MiddleWare_/Thirdparty/W5500/socket.c \
../_MiddleWare_/Thirdparty/W5500/wizchip_conf.c 

OBJS += \
./_MiddleWare_/Thirdparty/W5500/socket.o \
./_MiddleWare_/Thirdparty/W5500/wizchip_conf.o 

C_DEPS += \
./_MiddleWare_/Thirdparty/W5500/socket.d \
./_MiddleWare_/Thirdparty/W5500/wizchip_conf.d 


# Each subdirectory must supply rules for building sources it contributes
_MiddleWare_/Thirdparty/W5500/%.o _MiddleWare_/Thirdparty/W5500/%.su _MiddleWare_/Thirdparty/W5500/%.cyclo: ../_MiddleWare_/Thirdparty/W5500/%.c _MiddleWare_/Thirdparty/W5500/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xC -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"D:/Test_Jig/FIber_FLS_PROG/Drivers/App_Drivers" -I"D:/Test_Jig/FIber_FLS_PROG/Drivers/App_Drivers/W5500_if" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Services" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Thirdparty/W5500" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-_MiddleWare_-2f-Thirdparty-2f-W5500

clean-_MiddleWare_-2f-Thirdparty-2f-W5500:
	-$(RM) ./_MiddleWare_/Thirdparty/W5500/socket.cyclo ./_MiddleWare_/Thirdparty/W5500/socket.d ./_MiddleWare_/Thirdparty/W5500/socket.o ./_MiddleWare_/Thirdparty/W5500/socket.su ./_MiddleWare_/Thirdparty/W5500/wizchip_conf.cyclo ./_MiddleWare_/Thirdparty/W5500/wizchip_conf.d ./_MiddleWare_/Thirdparty/W5500/wizchip_conf.o ./_MiddleWare_/Thirdparty/W5500/wizchip_conf.su

.PHONY: clean-_MiddleWare_-2f-Thirdparty-2f-W5500

