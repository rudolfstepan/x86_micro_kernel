    .section .rodata.desktop_splash,"a",@progbits
    .balign 4
    .global reist_desktop_splash_bmp
    .type reist_desktop_splash_bmp,@object
reist_desktop_splash_bmp:
    .incbin "assets/images/reist-splash.bmp"
    .global reist_desktop_splash_bmp_end
reist_desktop_splash_bmp_end:
    .size reist_desktop_splash_bmp, reist_desktop_splash_bmp_end - reist_desktop_splash_bmp
    .balign 4
    .global reist_desktop_splash_bmp_size
    .type reist_desktop_splash_bmp_size,@object
reist_desktop_splash_bmp_size:
    .long reist_desktop_splash_bmp_end - reist_desktop_splash_bmp
    .size reist_desktop_splash_bmp_size, 4

    .section .note.GNU-stack,"",@progbits
