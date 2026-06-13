################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../_MiddleWare_/Services/command/command.c 

OBJS += \
./_MiddleWare_/Services/command/command.o 

C_DEPS += \
./_MiddleWare_/Services/command/command.d 


# Each subdirectory must supply rules for building sources it contributes
_MiddleWare_/Services/command/%.o _MiddleWare_/Services/command/%.su _MiddleWare_/Services/command/%.cyclo: ../_MiddleWare_/Services/command/%.c _MiddleWare_/Services/command/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xC -c -I../Core/Inc -I"D:/Test_Jig/FIber_FLS_PROG/Drivers/App_Drivers" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Services" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Thirdparty" -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-_MiddleWare_-2f-Services-2f-command

clean-_MiddleWare_-2f-Services-2f-command:
	-$(RM) ./_MiddleWare_/Services/command/command.cyclo ./_MiddleWare_/Services/command/command.d ./_MiddleWare_/Services/command/command.o ./_MiddleWare_/Services/command/command.su

.PHONY: clean-_MiddleWare_-2f-Services-2f-command

