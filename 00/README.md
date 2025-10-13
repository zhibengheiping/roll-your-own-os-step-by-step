# bake

用Python实现类似ninja的功能。ninja的问题是所有变量都是字符串，没办法共用一组文件名。

## subninja

include("sub/build.bk") 相当于 subninja sub/build.bk

此时，在sub/build.bk里，默认可以访问bakefile的模块变量。给这些变量赋值不会改变bakefile里的值。

这个仅限于模块作用域，模块内部的函数在模块加载完之后是无法访问上层的变量的

## 变量作用域

声明一个变量是全局变量，效果应该相当于ninja的变量

我们在lib.bm里声明了global g，

在bakefile里，g赋值之前，get_g()返回的还是0，g赋值之后get_g()返回的就是1了。

返回的永远是当前正在加载的模块里的值

PWD默认是全局变量。

## 多条规则创建同一个文件需要报错

./multi.bk

## 找不到目标需要报错

./simple.bk tesr.o

这个抄袭了Python的功能，提示test.o

## 循环依赖需要报错

./cycle.bk

## 日志

根据mtime判断文件是否变化

每个文件记录命令运行起始时间，命令行的hash值，以及运行结束后的mtime

假设运行的命令行hash值相同，且输入输出文件没有变化，我们就可以跳过这条规则

## 任务类型

- build
- clean 删除文件
- cleandead 删除在日志里，但已经没有规则能生成的文件
