---
description: Cooperative resumable Virtual Machine via Generators and Promises
---

# Non-blocking I/O

OCP program usually need to suspend and resume later for I/O.

```text
exec sql
        select Name where Dept = :Dept-Num
end-exec.
```

Obviously, an userland multi-tasking model will be implemented to not blocking the host's thread \(aka: non-blocking I/O\). CAM use `generator` and `promise` to support this class of problem. Generator is a lightweight program that can be cooperatively waiting for Promises, a proxy to value that may not available immediately.

## Value generator

Generator is somthing that generate value, usually used to abstract a series. Calling a generator will return a generator, of course \(=\]\). It's different to a traditional program, generator will not run until we tell it to run. Generator can yield \(suspend\) and return a value in the middle of its execution. Afterthat, when we tell a generator to run again, it will resume from where it was suspended.

```text
*> a series of natural numbers
generator-id. Natural-Numbers
        data division
                linkage section
                01 Start-Value Comp-4
                01 Current     Comp-4

        procedure division using Start-Value
                set Current to Start-Value

                *> infinity loop
                perform until false
                        yield Current *> suspend with a value
                        add Current 1 giving Current
                end-perform
end-generator Natural-Numbers
```

Generator could be used like this:

```text
program-id. Display-Natural-Numbers
        data division
                linkage section
                01 Value   Comp-4    value 5
                01 Numbers Generator

        procedure division
                *> Create a new natural number generators
                call Natural-Numbers using Value returning Numbers

                *> infinity loop
                perform until false
                        call Gen:Next using Numbers returning Value
                        display 'next number is: ' Value '\n'
                end-perform
end-program Display-Natural-Numbers
```

`Gen:Next` will run the generator until it encounters a yield statement and returning yielded value.

## Value promise

Promise is something that **may be** resolved to a value later.

```text
program-id. Main
        data division
                linkage section
                01 Name-Promise Promise

        procedure division
                call Sql:Run-Query using 'select Name from ...' returning Name-Promise

                call Promise:Then
                        using Name-Promise *> what to wait for
                              Display-Name *> callback on resolved
                end-call *> explicit end of scope for explicitly, optional
                         *> could be replaced by a f*cking dot (.) as-well
end-program Main

program-id. Display-Name
        data division
                linkage section
                01 Name Display

        procedure division using Name
                display 'name: ' Name
end-program Display-Name
```

`Promise:Then` accepts a promise and a callback when the SQL driver submit its value.

```text
*> SQL driver
call Promise:Resolve
        using Name-Promise *> what to submit value
              'John Caleb' *> value of promise
end-call
```

In case of error, SQL driver will throw an exception value.

```text
*> SQL driver
call Promise:Throw
        using Name-Promise *> where to throw
              'authfailed' *> exception string
end-call
```

Which could be handled in error callback.

```text
call Promise:Then
        using Name-Promise *> what to wait for
              Display-Name *> callback on resolved
end-call

call Promise:Catch
        using Name-Promise *> what to catch error
              Display-Fack *> callback on error
end-call
```

COBOL uses passed by reference by default, so it's the same as:

```javascript
namePromise = System.promiseThen  (namePromise, displayName)
namePromise = System.promiseCatch (namePromise, displayFack)
```

Basically, it will create two promises, the later will be chained to the former.

## Combined together

Callback is not what we want to use, even with anonymous syntax \(lambda\) it still suffers from callback pyramid. Furthermore, it will be a disaster to use callback with inline SQL in COBOL \(much of rewriting\). Instead of that, we could abuse generator to make the code reads just like it's synchronous, but in fact asynchronous.

```text
generator-id. Main
        data division
                linkage section
                01 Name-Promise Promise
                01 Name         Display

        procedure division
                call Sql:Run-Query using 'select Name from ...' returning Name-Promise
                set Name to yield Name-Promise
                display 'name: ' Name
end-generator Main
```

Or less tedious with OCP call expression syntax.

```text
generator-id. Main
        procedure division
                display 'name: ' yield Sql:Run-Query('select Name from ...')
end-generator Main
```

A scheduler also needed to run this generator.

```text
call Main returning Main-Task
call Scheduler:Add-Task using Main-Task
*> or may be less tedious
Scheduler:Add-Task(Main())
```

When a generator yield with a promise, it will be resumed later by the scheduler when that promise become `ready` \(resolved to a value or an exception thrown\). The resolved value will be passed as value of the yield expression. Pretty much like this:

```text
*> first iteration, runs until yield
call Gen:Next using Main-Task returning A-Promise

*> yielding for first promise
call Promise:Then using
        A-Promise *> promise by SQL:Run-Query
        lambda using Name *> anonymous syntax for callback
                call Gen:Next-With-Value
                        using     Main-Task  *> what to resume
                                  Name       *> resolved by SQL driver
                        returning Some-Thing *> another promise or end-value
                end-call
                *> if Some-Thing is another promise, repeat
        end-lambda
end-call
```

### Chaining of generators:

Asynchronous that call asynchronous:

```text
generator-id. Get-Name
        data division
                linkage section
                01 Name Display

        procedure division
                set Name to yield Sql:Run-Query('select Name from ...')
                call Format:To-Upper-Case using Name
                yield Name *> last yield is returning value
end-generator Get-Name

generator-id. Main
        data division
                linkage section
                01 Name-Promise Promise

        procedure division
                *> convert a generator to promise
                *> resolved after Get-Name last yield
                call Gen:As-Promise using Get-Name() returning Name-Promise
                display 'name: ' yield Name-Promise
end-generator Main
```

Or may be less tedious.

```text
generator-id. Main
        procedure division
                display 'name: ' yield Gen:As-Promise(Get-Name())
end-generator Main
```

### Multi-returnings

We can't yield multiple values, so it must be packed into a tuple.

```text
generator-id. Get-Employee
        data division
                linkage section
                01 Name   Display
                01 Salary Comp-4

        procedure division
                exec sql select Name, Salary from ... end-exec
                yield Tuple:Make-2(Name, Salary)
                *> or with tuple syntax
                yield { Name, Salary }
end-generator Get-Employee

generator-id. Main
        data division
                linkage section
                01 Employee-Promise Promise
                01 Employee         Tuple
                01 Employee-Name    Display
                01 Employee-Salary  Comp-4

        procedure division
                call Gen:As-Promise
                        using     Get-Employee()
                        returning Employee-Promise
                end-call

                set Employee to yield Employee-Promise

                call Tuple:Element-At using Employee, 0 returning Employee-Name
                call Tuple:Element-At using Employee, 1 returning Employee-Salary
                display 'name: ' Employee-Name ', salary: ' Employee-Salary
end-generator Main
```

## Async and await syntax

It's less verbose to abuse generator for asynchronous with a sugar syntax.

```text
program-id. Get-Employee async
        data division
                linkage section
                01 Name   Display
                01 Salary Comp-4

        procedure division returning Name, Salary
                exec sql select Name, Salary from ... end-exec
                call Format:To-Upper-Case using Name
end-program Get-Employee

program-id. Main async
        data division
                linkage section
                01 Name   Display
                01 Salary Comp-4

        procedure division
                await Get-Employee returning Name, Salary
                *> or with tuple de-construction syntax
                set { Name, Salary } to await Get-Employee()
                display 'name: ' Name ', salary: ' Salary
end-program Main
```

### Copy-pasted legacy code

Users will be warned about async / await by the compiler.

Await in a NOT async program e.g. EXEC SQL, EXEC CICS etc.

```text
program-id. Get-Employee
        data division
                linkage section
                01 Name   Display
                01 Salary Comp-4

        procedure division returning Name, Salary
                *> error: Get-Employee isn't an async program
                exec sql select Name, Salary from ... end-exec
                call Format:To-Upper-Case using Name
end-program Get-Employee
```

Regular call an async program will return a promise, so type error.

```text
program-id. Main
        data division
                linkage section
                01 Name   Display
                01 Salary Comp-4

        procedure division
                *> error: type mismatched, (Display vs Promise)
                *> you should use await statement for async program
                call Get-Employee returning Name, Salary
end-program Main
```

