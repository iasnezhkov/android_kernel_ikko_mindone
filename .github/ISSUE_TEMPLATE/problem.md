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
