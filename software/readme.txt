avrdude -c usbtiny -p t85 -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U flash:w:PhoneChargeGuard_v1.0.hex
