if not cc.has_function('dlsym')
    libdl = cc.find_library('dl')
else
    libdl = []
endif

add_project_arguments('-Wall', language : 'c')
add_project_arguments('-Werror', language : 'c')
add_project_arguments('-fvisibility=hidden', language : 'c')

if cc.has_argument('-Wpedantic')
    add_project_arguments('-Wno-pedantic', language : 'c')
endif

src = [
    'gbm-platform.c',
    'gbm-display.c',
    'gbm-utils.c',
    'gbm-mutex.c',
    'gbm-handle.c',
]

egl_gbm = library('nvidia-egl-gbm',
    src,
    dependencies : [
        eglexternalplatform,
        gbm,
        threads,
        libdl,
    ],
    version : meson.project_version(),
    install : true,
)
