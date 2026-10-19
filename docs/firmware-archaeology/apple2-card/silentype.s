;******************************************
; Silentype Printer Interface
;
; Code (c) by Apple Computer
; Source code (CC) 2010 by Marc S. Ressl
;******************************************

;******************************************
; Physical Interface
;
; Pins
;   1- Shield GND
;   2- +5V
;   3- Logic GND
;   4- Serial data
;   5- Limit sensor
;   6- +12V
;   7- Power GND
;   8- Register store clock (RSC)
;   9- Shift clock (SC)
;
; 74LS673 mapping
;   ~STRCLK- (!(RSC & SC) & 555 Timer)
;   SHCLK-   !SC
;
; 555 timer:
;   C- 0.1 uF
;   R- 50 kOhm
;   T- 5 ms
;
; The timer is triggered through the
; logical AND of the printer pin outputs.
;
;******************************************

;******************************************
; Logic Interface
;
; Writes ($c081)
;   Bit 2- Serial clock
;   Bit 3- Serial data
;   Bit 4- Register store
;
; Reads ($c084)
;   Bit 7- Limit sensor
;
; Serial Order
;
;   Bit 0-  Motor 1 Phase 0
;   Bit 1-  Motor 1 Phase 1
;   Bit 2-  Motor 1 Phase 2
;   Bit 3-  Motor 1 Phase 3
;   Bit 4-  Motor 2 Phase 0
;   Bit 5-  Motor 2 Phase 1
;   Bit 6-  Motor 2 Phase 2
;   Bit 7-  Motor 2 Phase 3
;   Bit 8-  R/W
;   Bit 9-  Printer Pin 1
;   Bit 10- Printer Pin 2
;   Bit 11- Printer Pin 3
;   Bit 12- Printer Pin 4
;   Bit 13- Printer Pin 5
;   Bit 14- Printer Pin 6
;   Bit 15- Printer Pin 7
;
;******************************************

;******************************************
; Addresses

zp1F		= $1f
textPosX	= $24
textPosY	= $25
grBaseL		= $26
grBaseH		= $27
stateStepper	= $27
textBaseL	= $2a
hiresCol	= $2a
textBaseH	= $2b
statePin	= $35
charOutL	= $36
charOutH	= $37
keyInL		= $38
keyInH		= $39
zpF6		= $f6
keyboardBuf	= $0100
screenTemp1	= $05b8
screenTemp2	= $07f8
keyboardData	= $c000
keyboardStrobe 	= $c010
ioBase		= $c080
ioBase1	 	= $c081
ioBase4		= $c084
ioDisableROM	= $cfff
monitorPutChar	= $fdf0
monitorSetKeyb	= $fe89
monitorSetVideo	= $fe93
monitorPrintErr	= $ff2d
monitorVector	= $ff58

;******************************************
; Defines

hiresRowNum	= $27
hiresLineNum	= 192

printerPinNum	= 7
printerRowNum	= 80

varStart	= $cf00
varIOBase	= $cf00

varDefault	= $cf01
varFlags	= $cf01
varCF02		= $cf02
varCF03		= $cf03
varCF04		= $cf04
varRowIndex	= $cf05
varStatePin	= $cf06
varCF07		= $cf07
varOrMask	= $cf08
varWaitLimit	= $cf09
varCF0B		= $cf0b
varScreenEcho	= $cf0c
varFormFeed	= $cf0d		; 0 to 255
varLineInc	= $cf0e		; 0 to 255
varDirection	= $cf0f		; Bit 7- 0 bidirectional, 1 unidirectional
varIntensity	= $cf10		; 0 to 7
varLeftMargin	= $cf11		; 0 to 82 ($53)
varRightMargin	= $cf12		; 1 to 83 ($54)
varHiresPage	= $cf13		; $20 (Page 1), $40 (Page 2)
varHiresInverse	= $cf14		; $ff (normal), $0 (inverse)
varStepper5	= $cf15
varHeadTab	= $cf16
varIntensityTab = $cf1e
varInitMark1	= $cf26
varInitMark2	= $cf27
varStepper3	= $cf28
varCurrRow	= $cf29	; Related to cf32
varChar		= $cf2a
varStepper1	= $cf2b
varStepper4	= $cf2c
varStepper2	= $cf2d
varCF2E		= $cf2e
varHiresLine	= $cf2f
varTemp1	= $cf30
varTemp2	= $cf31
varTemp3	= $cf32
varBitmap	= $cf33

;******************************************

*		= $C800

		BIT monitorVector
		BVS init
		SEC
		BCC $C820
		CLV
init:
		PHA
		TXA
		PHA
		TYA
		PHA
		PHP
		SEI
		JSR monitorVector
		TSX
		LDA keyboardBuf,X
		CMP #$C8
		BEQ init1
		BIT ioDisableROM
		JMP init2
init1:
		STA varTemp3
		PLP
		STY varIOBase
		JSR initPrinter1
		JMP flushBuffer7a
init2:
		STA screenTemp2
		ASL
		ASL
		ASL
		ASL
		STA varIOBase

		PLA
		PLA
		PLA
		PLA
		STA varChar
		AND #$7F
		STA zp1F
		STA varTemp3

		TXS
		PLP
		BVC init5
		BVS init3

		NOP
		STX zpF6
		JSR readKey
		STA varCF02
		LDX zpF6
		AND #$7F
		STA screenTemp1,X
		RTS
init3:
		JSR initPrinter1

		LDA screenTemp2
		CMP keyInH

		BNE init4
		STA charOutH

		LDA #$05
		STA keyInL

		LDA #$FF
		STA varCF02
init4:
		LDA #$07
		STA charOutL

		CLC
init5:
		BCS init6

		JSR handleChar
		JMP flushBuffer7a
init6:
		JMP flushBuffer7b



readKey:
		LDA #$F8
		STA varTemp1
readKey1:
		LDA keyboardData
		BMI readKey3

		DEY
		BNE readKey1

		DEC varTemp1
		BNE readKey1

		LDA #$FF
		STA varStepper5
		JSR someOther
readKey2:
		LDA keyboardData
		BPL readKey2
readKey3:
		BIT keyboardStrobe
		STA varChar
		RTS



handleChar:
		JSR echoChar
		LDA zp1F
handleChar0:
		CMP #$0D	; CTRL-M Return
		BNE handleChar1

		JSR doReturn

		BIT varTemp3
		BPL handleChar5a

		RTS
handleChar1:
		CMP #$0C	; CTRL-L Form feed
		BNE handleChar2
handleChar1a:
		JSR flushBuffer
		JSR doFormFeed0
		JMP doReturn1
handleChar2:
		CMP #$06	; CTRL-F Flush buffer
		BNE handleChar3
handleChar2a:
		LDA varCurrRow
		PHA
		JSR flushBuffer
		PLA
		STA varCurrRow
		STA varCF03

		BPL doReturn0
handleChar3:
		CMP #$11	; CTRL-Q Print hires screen
		BNE handleChar4

		LDA varRowIndex
		CMP #$52
		BCS handleChar4a

		JMP printHires
handleChar4:
		CMP #$14	; CTRL-T Toggle screen echo
		BNE handleChar5

		LDA varScreenEcho
		EOR #$FF
		STA varScreenEcho
handleChar4a:
		RTS
handleChar5:
		CMP #$0A	; CTRL-J Line feed
		BNE handleChar6

		JSR handleChar2a
handleChar5a:
		JMP initPrinter
handleChar6:
		PHA
		LDA varRightMargin
		CMP varCurrRow
		BCS handleChar7

		JSR doReturn
		JSR initPrinter
handleChar7:
		PLA
		STA zp1F
		JMP echoSub



doReturn:
		JSR flushBuffer
doReturn1:
		LDA varLeftMargin
		STA varCurrRow
		STA varCF03
		LDA #$00
		STA varCF04
		STA textPosX
doReturn0:
		BIT varCF02
		BPL doReturn2

		JSR initPrinter3
doReturn2:
		JMP doSomething



flushBuffer:
		LDY varRowIndex
flushBuffer1:
		CPY #printerRowNum
		BEQ flushBuffer4
		DEY
		LDA varStart,Y
		CMP #$20
		BNE flushBuffer2

		STY varRowIndex
		DEC varCurrRow
		BNE flushBuffer1
flushBuffer2:
		BIT varDirection
		BPL flushBiDir

		JSR initPrinter3
flushBuffer2a:
		LDA varCF03
		STA varCurrRow
		JSR printLine
		JSR disableFlag8

		LDY #printerRowNum
flushBuffer3:
		CPY varRowIndex
flushBuffer4:
		BCS flushBuffer11
		LDA varStart,Y
		STA zp1F
		TYA
		PHA
		JSR flushBuffer11a
		PLA
		TAY
		INY
		BNE flushBuffer3

flushBiDir:
		SEC
		LDA varCurrRow
		SBC varCF2E
		BPL flushBuffer9
flushBuffer6:
		JSR printLine
		JSR enableFlag8

		LDA #$02
		JSR someOther6
flushBuffer7:
		LDY varRowIndex
		CPY #printerRowNum
		BEQ flushBuffer10

		DEY
		STY varRowIndex
		LDA varStart,Y
		STA zp1F

		JSR flushBuffer11a

		JMP flushBuffer7
flushBuffer7b:
		JSR readKey
flushBuffer7a:
		PLA
		TAY
		PLA
		TAX
		PLA
		LDA varChar
flushBuffer7c:
		RTS
flushBuffer8:
		AND #$7F
flushBuffer8a:
		STA zp1F
		JMP handleChar0
flushBuffer9:
		STA textBaseL
		SEC
		LDA varCF2E
		SBC varCF03
		BMI flushBuffer2a

		CMP textBaseL
		BCS flushBuffer6
		BCC flushBuffer2a
flushBuffer10:
		JSR initPrinter3
flushBuffer11:
		LDA #printerRowNum
		STA varRowIndex
		RTS

flushBuffer11a:
		LDA zp1F
		CMP #$08	; CTRL-H Backspace
		BEQ flushBuffer13

		BIT varFlags
		PHP
		BPL flushBuffer12

		DEC varCF2E
		JSR someOther5
flushBuffer12:
		JSR initPrinter0b
		PLP
		BMI flushBuffer7c

		INC varCF2E
		JMP someOther5

flushBuffer13:
		JSR ohMyGod
		JSR someOther

ohMyGod:
		JSR doSomething
		JSR waitLimitSensor6b
		LDA varFlags
		EOR #$80
		JMP storeFlags

echoChar:
		BIT varScreenEcho
		BMI echoChar1

; Screen echo

		LDA textPosX
		CMP varCF04
		BCS echoChar1

		CMP #$11
		BCS echoChar1

		ORA #$F0
		AND varCF04
		ADC textPosX
		STA textPosX
echoChar1:
		BIT varCF02
		BMI echoChar4

		SEC
		LDA textPosX
		SBC varCF04
		BMI echoChar2
		BEQ echoChar2
		TAX
echoChar3:
		LDA #$20
		JSR echoSub1
		DEX
		BNE echoChar3
echoChar2:
		BIT varScreenEcho
		BPL echoChar4

		LDA varChar
		JMP monitorPutChar
echoChar4:
		CLC
		LDY #$00
		LDA varCurrRow
		SBC varRightMargin

		CMP #$F8
		BCC echoChar5

		ADC #$27
		TAY
echoChar5:
		STY textPosX
		RTS



echoSub:
		LDA zp1F
echoSub1:
		CMP #$08
		BNE echoSub2

		LDA varCurrRow
		BEQ echoSub3

		DEC varCurrRow
		DEC varCF04

		LDA #$08
		BNE echoSub4
echoSub2:
		CMP #$20
		BCC echoSub3

		INC varCF04
		INC varCurrRow
echoSub4:
		BIT varCF02
		BMI echoSub5

		LDY varRowIndex
		STA varStart,Y
		INY
		CPY #$FD
		BCC echoSub4a

		JMP flushBuffer
echoSub4a:
		STY varRowIndex
echoSub3:
		RTS
echoSub5:
		BIT varStepper5
		BPL echoSub6
		INC varStepper5
		JSR flushBuffer13
echoSub6:
		JSR flushBuffer11a
		JSR doSomething

		LDA varCF2E
		CMP varRightMargin
		BCC echoSub3

		JSR initPrinter
		JMP initPrinter3



resetPrinter:
		LDA #$00
sendStateBit:
		LDX varIOBase
		STA ioBase1,X
		RTS



sendState:
		ASL stateStepper
		TYA
		PHA
		LDX varIOBase
		LDA #$07
		LDY #$0F	; Send 16 bits

; Send data

sendState1:
		ASL statePin
		ROL stateStepper

		ROL
		STA ioBase1,X
		LSR

		DEY
		BPL sendState1

; Trigger store clock

		LDA #$1C
		STA ioBase1,X	; Write 0x1c
		LDA #$18
		STA ioBase1,X	; Write 0x18
		LDA #$1C
		STA ioBase1,X	; Write 0x1c
		LDA #$0C
		STA ioBase1,X	; Write 0x0c
		PLA
		TAY
		RTS



sendStateFlag8_0:
		LDA varStatePin
		LSR
		BCC sendStateFlag8_0a

		ORA #$08
sendStateFlag8_0a:
		STA varStatePin
sendStateFlag8_0b:
		STA statePin
		LDA #$00
		STA stateStepper

		JMP sendState

sendStateFlag8_1:
		LDA varStatePin
		ASL
		CMP #$10
		BCC sendStateFlag8_0a

		AND #$0F
		ORA #$01
		BNE sendStateFlag8_0a
sendStateFlag8_1a:
		LDA varCF07
		LSR
		BCC sendStateFlag8_1b
		ORA #$08
sendStateFlag8_1b:
		STA varCF07
sendStateFlag8_1c:
		ASL
		ASL
		ASL
		ASL
		BNE sendStateFlag8_0b

printDotColumn:
		TYA
		PHA
		LDA varStepper4
		PHA
		ORA varStepper3
		EOR #$FF
		AND varStepper1
		STA varStepper2
		PLA
		STA varStepper3
		LDA varStepper1
		STA varStepper4
		PHP
		SEI

		BIT varFlags
		BMI printDotColumn1

		JSR sendStateFlag8_0
		JMP printDotColumn2
printDotColumn1:
		JSR sendStateFlag8_1
printDotColumn2:
		LDA varStepper2
		STA stateStepper
		LDA varStatePin
		STA statePin

		LDY varIntensity
		SEC
		LDA varIntensityTab,Y
		SBC #$64
		TAX
printDotColumn3:
		DEX
		BNE printDotColumn3	; Intensity loop 1

		JSR sendState

		LDA varStepper1
		STA stateStepper
		LDA varStatePin
		STA statePin

		BIT varFlags
		BVC printDotColumn4

		LDA #$04
		JSR sleep
printDotColumn4:
		JSR sendState

		LDA #$00
		STA stateStepper
		LDA varStatePin
		STA statePin
		LDX varHeadTab,Y
printDotColumn5:
		PHA
		PLA
		NOP
		DEX
		BNE printDotColumn5	; Intensity loop 2

		JSR sendState

		LDX varIntensityTab,Y
printDotColumn6:
		DEX
		BNE printDotColumn6	; Intensity loop

		LDA #$BF		; Clear bit 6
		AND varFlags
		STA varFlags
		PLP
		PLA
		TAY
		RTS



sleep40:
		LDA #$27
sleep:
		PHP
		SEI
		STX varTemp2
sleep1:
		LDX #$12
sleep2:
		DEX
		BNE sleep2	; Sleep small loop

		TAX		; DEA
		DEX
		TXA

		BNE sleep1	; Sleep big loop

		LDX varTemp2
		PLP
		RTS

sleep08:
		LDA #$08
		BNE sleep

doSomething:
		LDA varStatePin
		JSR sendStateFlag8_0b
		LDA varFlags
		ORA #$40
		STA varFlags
doSomething1:
		JSR sleep40
		JMP resetPrinter



waitLimitSensor:
		LDA #$16
		STA varTemp1
waitLimitSensor1:
		LDY #$24
waitLimitSensor2:
		JSR sendStateFlag8_1
		JSR sleep40
		LDX varIOBase
		LDA ioBase4,X	; Read limit Sensor
		BMI waitLimitSensor4
		DEY
		BNE waitLimitSensor2
		DEC varTemp1
		BNE waitLimitSensor1
waitLimitSensor3:
		BIT varTemp3
		BMI waitLimitSensor8

		JSR monitorSetVideo
		JSR monitorSetKeyb
		JMP monitorPrintErr

waitLimitSensor4:
		LDY #$10
waitLimitSensor5:
		LDX varIOBase
		LDA ioBase4,X	; Read limit Sensor
		BPL waitLimitSensor6
		JSR sendStateFlag8_0
		JSR sleep40
		DEY
		BPL waitLimitSensor5
		BMI waitLimitSensor3
waitLimitSensor6:
		JSR disableFlag8
waitLimitSensor6c:
		LDA #$00
		STA varCF2E
		STA varCurrRow
		STA varStepper4
		STA varStepper3
		STA varStepper5
		JSR doSomething
waitLimitSensor6b:
		LDA #$F0
		JMP sleep
waitLimitSensor6a:
		JMP (varWaitLimit)

waitLimitSensor7:
		SEC
		SBC #$20
		PHA
		LDY grBaseL
		BIT varFlags
		BPL waitLimitSensor7a
		LDA #$04
		SBC grBaseL
		TAY
waitLimitSensor7a:
		LDA fontIndexL,Y
		STA textBaseL
		LDA fontIndexH,Y
		STA textBaseH
		PLA
		TAY
		LDA #$2C
		JSR sendStateBit
		LDA (textBaseL),Y
		PHA
		LDA #$0C
		STA ioBase1,X	; Write 0x0c
		PLA
		ORA varOrMask
		STA varStepper1
waitLimitSensor8:
		RTS



someOther5:
		LDA #$01
someOther6:
		STA grBaseL
		LDA #$00
		STA varStepper1
someOther6a:
		JSR printDotColumn
		DEC grBaseL
		BNE someOther6a
		RTS

someOther:
		LDA #$01
someOther4:
		TAY
		BEQ someOther1
someOther4a:
		LDA varCF0B
		JSR someOther6

		BIT varFlags
		BMI someOther2
		INC varCF2E
		BNE someOther3
someOther2:
		DEC varCF2E
someOther3:
		DEY
		BNE someOther4a
someOther1:
		RTS

yetOther:
		LDA varLeftMargin
		STA varCurrRow

printLine:
		JSR disableFlag8
		LDA varCurrRow
		SEC
		SBC varCF2E
		BPL yetOther2

		PHA
		JSR enableFlag8

		PLA
		EOR #$FF
		CLC
		ADC #$01
yetOther2:
		JMP someOther4

enableFlag8:
		LDA varFlags
		ORA #$80
storeFlags:
		STA varFlags
		RTS

disableFlag8:
		LDA varFlags
		AND #$7F
		BPL storeFlags

doFormFeed0:
		LDA varFormFeed
doFormFeed1:
		STA grBaseL
doFormFeed2:
		JSR sendStateFlag8_1a
		LDA #$6E
		JSR sleep
		DEC grBaseL
		BNE doFormFeed2

		LDA varCF07
		JSR sendStateFlag8_1c
		JSR waitLimitSensor6b
		JMP doSomething1

initPrinter0b:
		LDY varCF0B
		DEY
		DEY
		STY grBaseL
initPrinter0c:
		LDA zp1F
		JSR waitLimitSensor6a

		JSR printDotColumn
		DEC grBaseL
		BPL initPrinter0c
		RTS

initPrinter:
		CLC
		LDA varLineInc
		ADC #$04
		BNE doFormFeed1
initPrinter1:
		LDY #$04

		LDA varInitMark1
		EOR #$A5
		CMP varInitMark2

		PHP
		BEQ initPrinter2

		LDY #$28
initPrinter2:
		LDA defaultVars,Y
		STA varDefault,Y
		DEY
		BPL initPrinter2

		JSR resetPrinter

		PLP
		BEQ initPrinter3

		JSR initPrinter
initPrinter3:
		JSR waitLimitSensor
		JSR yetOther
		JMP doSomething

;******************************************

defaultVars:	.byte $00, $00, $02, $00, $50, $03, $03, $00
		.byte $1B, $CC, $06, $00, $28, $02, $00, $05
		.byte $02, $51, $20, $FF, $00, $28, $3C, $50
		.byte $64, $78, $8C, $A0, $C8, $FA, $DC, $C4
		.byte $A0, $8C, $78, $69, $65, $B7, $12

;******************************************

printHires:
		LDY #printerRowNum
printHires1:
		JSR sleep
		DEY
		BNE printHires1

		JSR flushBuffer11
		JSR initPrinter3
		LDA #$00
		STA varHiresLine
printHires2:
		LDY #hiresRowNum
printHires3:				; Print line loop
		JSR printHiresBlock

		LDA keyboardData
		CMP #$83		; CTRL-C
		BEQ printHires6

		DEY
		BPL printHires3

		JSR waitLimitSensor6c
		LDA #$04
		JSR doFormFeed1

		BIT varDirection
		BMI printHires4

		JSR ohMyGod
		PHP
		JSR someOther5
		JSR sleep08
		JSR someOther5
		PLP
		BMI printHires5
printHires4:
		JSR initPrinter3
printHires5:
		CLC

		LDA #printerPinNum
		ADC varHiresLine
		STA varHiresLine
		CMP #hiresLineNum
		BCC printHires2
printHires6:
		BIT keyboardStrobe
		JSR handleChar1a
		JMP initPrinter3

;******************************************

printHiresBlock:
		LDX #$06		; 7 pixels per block

		TYA
		PHA
		BIT varFlags
		BMI printHiresBlock1
		STY hiresCol
		LDA #$27
		SEC
		SBC hiresCol
		TAY

; Get block

printHiresBlock1:
		CLC
		TXA
		ADC varHiresLine
		CMP #hiresLineNum
		BCC printHiresBlock2

		LDA #$00		; Last line
		BEQ printHiresBlock3
printHiresBlock2:
		PHA
		AND #$C0
		STA grBaseL
		LSR
		LSR
		ORA grBaseL
		STA grBaseL
		PLA

		STA grBaseH
		ASL
		ASL
		ASL
		ROL grBaseH
		ASL
		ROL grBaseH
		ASL
		ROR grBaseL

		LDA grBaseH
		AND #$1F
		ORA varHiresPage
		STA grBaseH

		LDA (grBaseL),Y
		EOR varHiresInverse

		BIT varFlags
		BPL printHiresBlock3
		ASL
printHiresBlock3:
		STA varBitmap,X
		DEX
		BPL printHiresBlock1	; Loop

; Print block

		LDY #$06		; 7 pixels per block
printHiresBlock3a:
		LDX #$06
		LDA #$00
		STA hiresCol

printHiresBlock4:
		BIT varFlags
		BMI printHiresBlock5

		LSR varBitmap,X
		BPL printHiresBlock6
printHiresBlock5:
		ASL varBitmap,X
printHiresBlock6:
		ROR hiresCol
		DEX
		BPL printHiresBlock4	; Prepare dot column

		LDA hiresCol
		LSR
		STA varStepper1
		JSR printDotColumn

		DEY
		BMI printHiresBlock7

		JSR sleep08
		JMP printHiresBlock3a	; Loop dot column
printHiresBlock7:
		PLA
		TAY
		RTS

fontIndexL:	.byte $94, $34, $D4, $74, $14
fontIndexH:	.byte $CF, $CF, $CE, $CE, $CE

fontRow5:	.byte $00, $00, $00, $14, $12, $62, $36, $00
		.byte $1C, $00, $08, $08, $00, $08, $00, $02
		.byte $3E, $00, $23, $42, $0C, $72, $3E, $40
		.byte $36, $32, $00, $00, $08, $14, $00, $20
		.byte $3E, $3F, $7F, $3E, $7F, $7F, $7F, $3E
		.byte $7F, $00, $02, $7F, $7F, $7F, $7F, $3E
		.byte $7F, $3E, $7F, $32, $40, $7E, $7C, $7F
		.byte $63, $70, $43, $00, $20, $00, $04, $01
		.byte $00, $02, $7F, $0E, $0E, $0E, $08, $08
		.byte $7F, $00, $02, $7F, $00, $0F, $1F, $0E
		.byte $1F, $08, $1F, $08, $10, $1E, $1C, $1E
		.byte $11, $18, $11, $08, $00, $00, $08, $2A
fontRow4:
		.byte $00, $00, $70, $7F, $2A, $64, $49, $10
		.byte $22, $00, $2A, $08, $01, $08, $03, $04
		.byte $45, $21, $45, $41, $14, $51, $49, $47
		.byte $49, $49, $1B, $01, $14, $14, $41, $40
		.byte $41, $44, $49, $41, $41, $49, $48, $41
		.byte $08, $41, $01, $08, $01, $10, $10, $41
		.byte $48, $41, $48, $49, $40, $01, $02, $04
		.byte $14, $08, $45, $7F, $10, $41, $08, $01
		.byte $40, $15, $11, $11, $11, $15, $3F, $15
		.byte $08, $11, $01, $04, $41, $10, $08, $11
		.byte $14, $14, $08, $15, $3E, $01, $02, $01
		.byte $0A, $05, $13, $36, $00, $41, $10, $55
fontRow3:
		.byte $00, $7D, $00, $14, $7F, $08, $35, $20
		.byte $41, $41, $1C, $3E, $07, $08, $03, $08
		.byte $49, $7F, $49, $49, $24, $51, $49, $48
		.byte $49, $49, $1B, $17, $22, $14, $22, $4D
		.byte $5D, $44, $49, $41, $41, $49, $48, $49
 		.byte $08, $7F, $01, $14, $01, $08, $08, $41
		.byte $48, $45, $48, $49, $7F, $01, $01, $08
		.byte $08, $0F, $49, $41, $08, $41, $10, $01
		.byte $20, $15, $11, $11, $11, $15, $48, $15
		.byte $10, $5F, $01, $0A, $7F, $0E, $10, $11
		.byte $14, $14, $10, $15, $11, $01, $01, $0E
		.byte $04, $05, $15, $41, $7F, $41, $08, $2A
fontRow2:
		.byte $00, $00, $70, $7F, $2A, $13, $02, $40
		.byte $00, $22, $2A, $08, $06, $08, $00, $10
		.byte $51, $01, $49, $59, $7F, $51, $49, $50
		.byte $49, $49, $00, $16, $41, $14, $14, $50
		.byte $49, $44, $49, $41, $41, $49, $48, $49
		.byte $08, $41, $01, $22, $01, $10, $04, $41
		.byte $48, $42, $4C, $49, $40, $01, $02, $04
		.byte $14, $08, $51, $41, $04, $7F, $08, $01
		.byte $10, $15, $11, $11, $11, $15, $40, $15
		.byte $10, $01, $5E, $11, $01, $10, $10, $11
		.byte $14, $14, $10, $15, $01, $01, $02, $01
		.byte $0A, $05, $19, $41, $00, $36, $04, $55
fontRow1:
		.byte $00, $00, $00, $14, $24, $23, $05, $00
		.byte $00, $1C, $08, $08, $00, $08, $00, $20
		.byte $3E, $00, $31, $66, $04, $4E, $26, $60
		.byte $36, $3E, $00, $00, $00, $14, $08, $20
		.byte $39, $3F, $36, $22, $3E, $41, $40, $2E
		.byte $7F, $00, $7E, $41, $01, $7F, $7F, $3E
		.byte $30, $3D, $33, $26, $40, $7E, $7C, $7F
		.byte $63, $70, $61, $00, $02, $00, $04, $01
		.byte $00, $0F, $0E, $00, $7F, $08, $20, $1E
		.byte $0F, $00, $00, $00, $00, $0F, $0F, $0E
		.byte $08, $1F, $08, $02, $02, $1E, $1C, $1E
		.byte $11, $1E, $11, $00, $00, $08, $08, $2A
extra:
		.byte $C1, $CA, $C8, $C1, $D0, $D0, $CC, $C5
		.byte $FF, $FF, $FF, $FF
