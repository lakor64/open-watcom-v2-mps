.386
.MODEL small
.data

extrn foo:near
extrn bar:far

extrn a:byte
extrn b:word
extrn d:dword
extrn e:fword
extrn f:tbyte

;moose db 12
dd 500 dup ( ? )
.code

ifdef errors
    jmp byte ptr [bx]
    jmp qword ptr [bx]
    jmp tbyte ptr [bx]
endif

; stuff with registers

jmp bx

jmp ebx

jmp word ptr [bx]
jmp dword ptr [bx]
jmp fword ptr [bx]

jmp word ptr [ebx]
jmp dword ptr [ebx]
jmp fword ptr [ebx]

jmp word ptr [ebx+2]
jmp dword ptr [ebx+2]
jmp fword ptr [ebx+2]

; near labels in other segs

jmp word ptr [foo]
jmp dword ptr [foo]
jmp fword ptr [foo]


jmp word ptr [bar]
jmp dword ptr [bar]
jmp fword ptr [bar]

jmp word ptr [bar+2]
jmp dword ptr [bar+3]
jmp fword ptr [bar+4]

;jmp a
a_near_lbl: jmp b
jmp d
jmp e
;jmp f

ifdef masmerrors
    jmp near ptr a
    jmp near ptr b
    jmp near ptr d
    jmp near ptr e
    jmp near ptr f
endif

jmp far ptr a
jmp far ptr b
jmp far ptr d
jmp far ptr e
jmp far ptr f

ifdef fubar
jmp far a
jmp far b
jmp far d
jmp far e
jmp far f
endif

jmp a_near_lbl
jmp short a_near_lbl
ifdef masmerrors
    jmp near a_near_lbl
    jmp far a_near_lbl
    jmp short ptr a_near_lbl
endif

jmp near ptr a_near_lbl
jmp far ptr a_near_lbl

jmp word ptr cs:a_near_lbl
jmp dword ptr cs:a_near_lbl
jmp fword ptr cs:a_near_lbl

if 0
jmp word ptr ds:a_near_lbl
jmp dword ptr ds:a_near_lbl
jmp fword ptr ds:a_near_lbl
endif

ifdef fubar
;    jmp moose          ; warning
;    jmp short moose    ; warning
ifdef masmerrors
    jmp near moose
    jmp far moose
    jmp short ptr moose
endif

;    jmp near ptr moose ; warning
jmp far ptr moose

jmp word ptr cs:moose
jmp dword ptr cs:moose
jmp fword ptr cs:moose

jmp word ptr ds:moose
jmp dword ptr ds:moose
jmp fword ptr ds:moose
endif

jmp word ptr [foo+2]                ; wasm error
jmp dword ptr [foo+3]
jmp fword ptr [foo+4]

; far labels in other segs
ifdef masmerrors
jmp bar
jmp [bar]
;jmp [bar+2]
;jmp [bar]+2

jmp foo
jmp [foo]
jmp [foo+2]
jmp [foo]+2
endif


end
