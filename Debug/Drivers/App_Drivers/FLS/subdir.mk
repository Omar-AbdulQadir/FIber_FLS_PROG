################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/App_Drivers/FLS/w25qxx.c 

OBJS += \
./Drivers/App_Drivers/FLS/w25qxx.o 

C_DEPS += \
./Drivers/App_Drivers/FLS/w25qxx.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/App_Drivers/FLS/%.o Drivers/App_Drivers/FLS/%.su Drivers/App_Drivers/FLS/%.cyclo: ../Drivers/App_Drivers/FLS/%.c Drivers/App_Drivers/FLS/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xC -c -I../Core/Inc -I"D:/Test_Jig/FIber_FLS_PROG/Drivers/App_Drivers" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Services" -I"D:/Test_Jig/FIber_FLS_PROG/_MiddleWare_/Thirdparty" -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-App_Drivers-2f-FLS

clean-Drivers-2f-App_Drivers-2f-FLS:
	-$(RM) ./Drivers/App_Drivers/FLS/w25qxx.cyclo ./Drivers/App_Drivers/FLS/w25qxx.d ./Drivers/App_Drivers/FLS/w25qxx.o ./Drivers/App_Drivers/FLS/w25qxx.su

.PHONY: clean-Drivers-2f-App_Drivers-2f-FLS

