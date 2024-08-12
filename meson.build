project('egl-gbm', 'c',
        version : '1.1.2',
        default_options : [
          'buildtype=debugoptimized',
          'c_std=gnu99',
          'warning_level=1',
        ],
        license : 'MIT',
        meson_version : '>= 0.50'
)

cc = meson.get_compiler('c')

egl_gbm_version = meson.project_version()
var_arr = egl_gbm_version.split('.')

egl_gbm_major_version = var_arr[0]
egl_gbm_minor_version = var_arr[1]
egl_gbm_micro_version = var_arr[2]

dep_libdrm = dependency('libdrm', version: '>= 2.4.75')

eglexternalplatform = dependency('eglexternalplatform', version : ['>=1.1', '<2'])

ext_includes = include_directories('external')

gbm = dependency('gbm', version : ['>=21.2'])
threads = dependency('threads')

pkgconf = configuration_data()
pkgconf.set('prefix',      get_option('prefix'))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir',      '${exec_prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir',  '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('datadir',     '${datarootdir}')
pkgconf.set('datarootdir', '${prefix}/@0@'.format(get_option('datadir')))

pkgconf.set('PACKAGE',                           meson.project_name())
pkgconf.set('EGL_GBM_EXTERNAL_VERSION',          meson.project_version())
pkgconf.set('EGL_EXTERNAL_PLATFORM_MIN_VERSION', '@0@.@1@'.format(egl_gbm_major_version, egl_gbm_minor_version))
pkgconf.set('EGL_EXTERNAL_PLATFORM_MAX_VERSION', egl_gbm_major_version.to_int() + 1)

subdir('src')
