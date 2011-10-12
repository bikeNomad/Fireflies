
set path=.,../common
if filereadable("/usr/avr/include/avr/io.h")
    set path+=/usr/avr/include
elseif filereadable("/usr/local/CrossPack-AVR/avr/include/avr/io.h")
    set path+=/usr/local/CrossPack-AVR/avr/include
elseif filereadable("/opt/local/avr/include/avr/io.h")
    set path+=/opt/local/avr/include
else
    echoerr "Can't find avr/io.h"
endif

set equalprg=gnuindent

if has("unix")
set grepprg=grep\ -nH\ --exclude\ '*/.svn/*'\ $*\ /dev/null
" elseif has("win32")
endif

au FileType c,cpp set ts=4 sw=4 et ai
