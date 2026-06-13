################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/App_Drivers/W5500_if/W5500_if.c 

OBJS += \
./Drivers/App_Drivers/W5500_if/W5500_if.o 

C_DEPS += \
./Drivers/App_Drivers/W5500_if/W5500_if.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/App_Drivers/W5500_if/%.o Drivers/App_Drivers/W5500_if/%.su Drivers/App_Drivers/W5500_if/%.cyclo: ../Drivers/App_Drivers/W5500_if/%.c Drivers/App_Drivers/W5500_if/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xC -c -I../Core/Inc -I"D:/Test_Jig/FIber_FLS_PROG/Drivers/App_Drivers" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Services" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Thirdparty" -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-App_Drivers-2f-W5500_if

clean-Drivers-2f-App_Drivers-2f-W5500_if:
	-$(RM) ./Drivers/App_Drivers/W5500_if/W5500_if.cyclo ./Drivers/App_Drivers/W5500_if/W5500_if.d ./Drivers/App_Drivers/W5500_if/W5500_if.o ./Drivers/App_Drivers/W5500_if/W5500_if.su

.PHONY: clean-Drivers-2f-App_Drivers-2f-W5500_if

