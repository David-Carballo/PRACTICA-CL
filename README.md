# PRACTICA-CL

The lab project will consist of building a compiler for an simplified imperative language named ASL (standing for A Simple Language).

The project will consist of:

Writting a grammar for the language, and use ANTLR4 to generate a parser in C++

Writting a semantic check module (in C++) that performs semantic analysis (i.e. variable type checking) on the tree produced by the parser on a given input program.

Writting a code generation module (in C++) that generates low-level code for the input program. We will generate simple 3-address code that will run on a custom virtual machine called tVM (standing for t-code Virtual Machine).

In order to do the work, you will have some help:

You will be using ANTLR-4, a powerful tool that will make it much easier to build your compiler.

You will be provided with a basic working compiler (with just assignment instructions and sum operations) which you will need to complete until your ASL compiler is finished.

You will also be provided with some auxiliary modules to help manage the data structures required to build the compiler.

Finally, you will be provided with a list of ASL programs to incrementally test your compiler.


### EXAMPLE OF CODE 
```C++
func f1(a: int) : int
  return a+1;
endfunc

func f2(b: int) : int
  return 2*b;
endfunc

func f3(c: int)
  write "hola";
endfunc

func main()
  var a,b,c : int
  var r : float
  var v : array[12] of int
  write a%b/3.5;
  write a%r + v;
  write 3 < d%a;
  c = a%b + b%a;
  c = v%a and a%v;
  f1 = f2;
  f1 = f3;
endfunc
```
