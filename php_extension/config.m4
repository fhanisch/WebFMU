PHP_ARG_ENABLE(fmu_extension, whether to enable fmu extension,
[ --enable-fmu-extension   Enable fmu extension])
 
if test "$PHP_FMU_EXTENSION" = "yes"; then
  AC_DEFINE(HAVE_FMU_EXTENSION, 1, [Whether you have fmu extension])
  PHP_NEW_EXTENSION(fmu_extension, fmu_extension.c, $ext_shared)
fi
