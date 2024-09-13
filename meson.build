project('qemu-plugins', 'c')

glib_dep = dependency('glib-2.0')
qemu_inc = include_directories('/nfs/home/yanyue/tools/qemu/include/qemu')

plugins = ['bb', 'empty', 'inline', 'insn', 'mem', 'syscall']

foreach plugin : plugins
  shared_module(plugin,
    sources: plugin + '.c',
    include_directories: qemu_inc,
    dependencies: glib_dep,
    name_prefix: 'lib',
    install: false
  )
endforeach

run_target('run',
  command: ['qemu-riscv64', '-plugin', './libinsn.so,match=ecall', '-d', 'plugin', '/nfs/home/yanyue/tests/riscv_tests/hello']
)
