---
name: Problem report
about: Something does not build, load, or work
---

**Device**
- [ ] iKKO MindOne
- [ ] another MT6789 / MT8781 device (which one?)

**Versions**
- Kernel release (`uname -r` or `include/config/kernel.release`):
- clang version (`clang --version`):
- Branch (`android14-6.1` or `android16-6.12`):

**What you ran**
```
(exact commands)
```

**What happened**
```
(actual output; for a module that will not load, include dmesg around insmod
 and: strings <module>.ko | grep vermagic)
```

**What you expected**
