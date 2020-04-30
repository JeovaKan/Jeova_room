# 关于makefile

## 是什么？

* 描述工程编译中，文件间依赖关系、文件编译顺序、中间生成文件、最终生成文件等信息的一个脚本；
* 用于linux下GUN gcc, UNIX 下 cc，均为C编译器；
* 基于文件依赖规则，定义了相关路径、被编译的文件。

## 怎么用？

* 在包含Makefile的文件路径下：
	1. make命令执行编译；
	2. make clean清除上一次编译的输出文件、所有中间文件。

## 怎么写？

**核心规则**

	target ...： prerequisites...
		command

* target:　可以是目标文件*.o，可以是中间文件，可以是可执行文件，也可以是标签；
* prerequisites: 生成target所依赖的一些文件或者目录；
* command: 生成规则；

		西红柿炒蛋：西红柿.c、鸡蛋.c、配料.h
			炒它
***
1. make本身不负责命令怎样工作，只是按照命令执行；
2. make将会检查target文件和prerequisites文件的修改时间，如果prerequisites比target新或者target文件尚未生成，就执行命令；
3. 例外的一点是，clean不是一个文件，是一个动作的名字。类似的，可以使用makefile对文件进行打包。

***

**输入make命令后发生了什么？**

* 当前路径下，shell下输入make命令以后。就在当前路径查找有没有Makefile或makefile名称的文件；
* 如果有文件，会在文件中找到第一个目标文件。
* 目标文件不存在或者没有依赖文件的修改日期新时，就需要重新按照规则生成；
* 同理如果被依赖的文件不存在，就要向下一级查找，同样判断并按规则生成；
* 最终所有文件都更新以后，新的目标文件也就生成了；
* 以上期间发生任何错误，如最后的依赖文件不存在的情况，make就直接退出了。
***
### 引入变量

**简化同名的重复引用**

	package := main.o display.o uart_com.o

	prj.o : $(package)
		gcc -o prj $(package)

	$(package) :　head_package.h
			

* 当最终的目标文件具有很多的依赖时，再增加新的依赖文件就变得很麻烦。
* 通过申明一个新的符号，包含所需要的文件，再打包传递给目标文件。

**使用include符号包含其他makefile**

	include top.make device.mk
* 可以是具体的makefile文件，也可以是自己定义的符号；
* make命令会根据命令的参数进行搜查，无参数时从当前目录查找，-include-dir参数设置时，就从参数指定的位置查找。

**使用通配符**

***
## 分层编写makefile

