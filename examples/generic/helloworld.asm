.ORG 0x8000
JMP _start

message:
.ASCII "Hello, World!"
.DB 0

_start:
LDX #0

loop:
LDA message, X
CMP #0
BEQ done
STA $F0
INX
JMP loop

done:
BRK