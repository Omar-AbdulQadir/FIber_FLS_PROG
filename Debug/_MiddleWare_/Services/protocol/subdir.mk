################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../_MiddleWare_/Services/protocol/protocol.c 

OBJS += \
./_MiddleWare_/Services/protocol/protocol.o 

C_DEPS += \
./_MiddleWare_/Services/protocol/protocol.d 


# Each subdirectory must supply rules for building sources it contributes
_MiddleWare_/Services/protocol/%.o _MiddleWare_/Services/protocol/%.su _MiddleWare_/Services/protocol/%.cyclo: ../_MiddleWare_/Services/protocol/%.c _MiddleWare_/Services/protocol/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xC -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"D:/Test_Jig/FIber_FLS_PROG/Drivers/App_Drivers" -I"D:/Test_Jig/FIber_FLS_PROG/Drivers/App_Drivers/W5500_if" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Services" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Thirdparty/W5500" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-_MiddleWare_-2f-Services-2f-protocol

clean-_MiddleWare_-2f-Services-2f-protocol:
	-$(RM) ./_MiddleWare_/Services/protocol/protocol.cyclo ./_MiddleWare_/Services/protocol/protocol.d ./_MiddleWare_/Services/protocol/protocol.o ./_MiddleWare_/Services/protocol/protocol.su

.PHONY: clean-_MiddleWare_-2f-Services-2f-protocol

