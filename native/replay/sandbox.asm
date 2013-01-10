; Shader assembler test
; used by ./exec_replay ps_sandbox_etna
; input:
;    u0.x = 0
;    u0.y = 1
;    u0.z = 0.5
;    u0.w = 2.0
;    u1.x = 1.0/256.0
MOV t2, void, void, t1
;MOV t1, void, void, u0.yyyy ; set to ones
MOV t1, void, void, u0.xxxx ; set to zeros

ADD t3, t2, void, -u0.z ; t3=t2-0.5

;SELECT.GE t1.x, t2.x, t2.y, t2.x  ; t1.x = MIN(t1.x,t1.y)
;SELECT.LE t1.x, t1.x, t1.y, t1.x  ; t1.x = MAX(t1.x,t1.y)

;LITP t1, t3.zxxz, t2.zyyz, void; u0.y 
; src0.y, src0.z
; src1.y, src1.z

;DST t1, t2.x, t2.y, void
;MOV t1.w, void, void, u0.z
;FRC t1, void, void, t3
;SIGN t1, void, void, -t0
;ADDLO t1, t1, void, u0.yyyy
MOV t1, void, void, t0
;MUL t1, t1, u1.x, void
NOP void, void, void, void
