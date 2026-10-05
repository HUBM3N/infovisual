#pragma once

// Horizontal and vertical Gaussian blur pixel shaders (ps_2_0), base85-encoded.
// Decoded at first use in BlurRT_Init — no global initializers, no constexpr.

static const char k_blurX85[] =
    "%/P:vP>$(#>T$<8?####Q$###%/P:v%####?####$&###J$###h####&####$####t#########.$###%####$####:$########tn=j=$8HlEQ2TuY3l:$#%),##$#########0g'WC"
    "`-.:CVjSuY&5>##%),##$#########C,.33UnSc;'AViF6JrEH<Sk:0+bVe=K&&PDlf1eGdfX1F$*fUCs'el1K>,C5AH3I3b48(#$QUV$)%XHVd;#K7#####X/'.7`7r'7$QUV$*%XHV"
    "d:i[7bmhf6##########D5YY#NSi.L,nHS[D5YY#_9r:Q0=XHVi>uu#^XF0LdfIl[[BA`V&5YY#W.]/Lpu$GV+>uu#PYr.LOV%JLou$GV&5YY#Q`%/Lpv*PV(>uu#Sf./L5hJcLdfIl["
    "(>uu#Rf./L4_/GLdfIl[&5YY#Y.]/Lqu$GV+>uu#RYr.LQV%JLou$GV&5YY#S`%/Lpv*PV(>uu#Uf./L7hJcLdfIl[(>uu#Tf./L6_/GLdfIl[i>uu#_XF0L4_/GL[BA`Vi>uu#`XF0L"
    "5_/GL[BA`Vi>uu#aXF0L6_/GL[BA`Vi>uu#bXF0L7_/GL[BA`V+>uu#W(S/L5_/GLpw0YV+G:;$W(S/L3_/GLpx6cV5_/GL+G:;$V(S/L4_/GLpw0YV5_/GL+G:;$V(S/L7_/GLqv*PV"
    "4_/GL+G:;$U(S/L6_/GLqv*PV4_/GL&5YY#fqF0L3_/GL#),##";

static const char k_blurY85[] =
    "%/P:vP>$(#>T$<8?####Q$###%/P:v%####?####$&###J$###h####&####$####t#########.$###%####$####:$########tn=j=$8HlEQ2TuY3l:$#%),##$#########0g'WC"
    "Qk;nDhpF/#&5>##%),##$#########C,.33UnSc;'AViF6JrEH<Sk:0+bVe=K&&PDlf1eGdfX1F$*fUCs'el1K>,C5AH3I3b48(#$QUV$)%XHVd;#K7NSi.LX/'.7`7r'7$QUV$*%XHV"
    "d:i[7bmhf6##########D5YY#NSi.L,nHS[D5YY#_9r:Q0=XHVi>uu#^XF0LdfIl[[BA`V&5YY#W.]/Lpu$GV+>uu#PYr.LOV%JLou$GV&5YY#Q`%/LP].JL&5YY#PYr.Lpv*PV(>uu#"
    "Rf./L4_/GLdfIl[&5YY#QYr.L)[-S[+G:;$R`%/Lou$GVOV%JL)]3][&5YY#Y.]/Lqu$GV+>uu#Sl7/LQV%JLou$GV&5YY#S`%/LP_:]L&5YY#RYr.Lpv*PV(>uu#Tf./L6_/GLdfIl["
    "&5YY#SYr.L)[-S[+G:;$T`%/Lou$GVQV%JL)]3][i>uu#_XF0L4_/GL[BA`Vi>uu#`XF0L5_/GL[BA`Vi>uu#aXF0L6_/GL[BA`Vi>uu#bXF0L7_/GL[BA`V+>uu#V(S/L4_/GLpw0YV"
    "+G:;$V(S/L3_/GLpx6cV4_/GL+G:;$V(S/L5_/GLpw0YV4_/GL+G:;$V(S/L6_/GLqv*PV4_/GL+G:;$U(S/L7_/GLqv*PV4_/GL&5YY#fqF0L3_/GL#),##";

// Decode base85 into shader bytecode. Returns byte count written, or 0 on failure.
// srcLen = length without null terminator. outCap must be >= (srcLen/5)*4.
inline int BlurDecodeShader(const char* src, int srcLen, unsigned char* out, int outCap)
{
    if (srcLen <= 0 || srcLen % 5 != 0) return 0;
    const int needed = (srcLen / 5) * 4;
    if (needed > outCap) return 0;
    for (int i = 0, j = 0; i < srcLen; i += 5, j += 4) {
        auto b = [](unsigned int c) -> unsigned int { return c >= (unsigned int)'\\' ? c - 36u : c - 35u; };
        unsigned int t = b((unsigned int)(unsigned char)src[i])
            + 85u * (b((unsigned int)(unsigned char)src[i+1])
            + 85u * (b((unsigned int)(unsigned char)src[i+2])
            + 85u * (b((unsigned int)(unsigned char)src[i+3])
            + 85u *  b((unsigned int)(unsigned char)src[i+4]))));
        out[j]   = (unsigned char)(t);
        out[j+1] = (unsigned char)(t >> 8);
        out[j+2] = (unsigned char)(t >> 16);
        out[j+3] = (unsigned char)(t >> 24);
    }
    return needed;
}
