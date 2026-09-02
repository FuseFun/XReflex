# XReflex 反射库

## 1. 简介

`XReflex` 是一个轻量级的 C++ 反射框架，它允许程序在**运行时**查询类的信息（类名、属性、方法），动态访问对象的属性值，以及通过名称调用成员函数。这种能力通常用于序列化、脚本绑定、编辑器、RPC 等场景。

该库通过**模板元编程**和**宏**来简化反射信息的注册。使用者只需在类定义中插入几个宏，并在类外使用宏注册属性和方法，即可获得反射能力。

---

## 2. 核心组件解析

### 2.1 `XTypeId` — 类型唯一标识

- 利用编译器预定义宏（如 `__PRETTY_FUNCTION__` 或 `__FUNCSIG__`）获取包含类型名的字符串，再通过 FNV-1a 哈希算法生成一个 `uint64_t` 的 ID。
- `XTypeId::Get<T>()` 在编译期生成类型 T 的唯一 ID。
- 用于在运行时比较类型是否一致（如判断 `XAny` 中存储的类型、属性类型检查等）。

### 2.2 `XAny` — 类型擦除的万能容器

- 可以存储任意类型的值（通过构造函数模板推导），并在运行时安全地取出。
- 内部采用**小对象优化（SOO）**：小于等于 32 字节且对齐要求不高的对象直接存储在内部的栈缓冲区 `StackStorage` 中；否则在堆上分配，栈中只存指针。
- 存储了三个函数指针：`Deleter`（析构对象）、`Mover`（移动构造或移动赋值）、以及类型 ID。
- 支持移动语义，但禁止拷贝（因为拷贝任意类型是困难的）。
- 通过 `Get<T>()` 取出存储的值，内部会进行类型检查（断言）。

**用途**：作为反射函数调用的返回值容器，使函数可以返回任意类型。

### 2.3 `XArg` — 函数参数的统一封装

- 包含一个 `XTypeId` 和一个 `void*` 指针，指向实际的参数数据。
- 提供 `As<T>()` 方法将参数转换为目标类型 T。
- 支持**快速路径**（类型完全匹配）和**字符串转换**：如果存储的是 `const char*`，可以转换为 `std::string` 或 `std::string_view`（常用于脚本调用时传递字符串）。

**用途**：在反射调用函数时，将调用者传入的参数统一打包为 `XArg` 数组，由框架分发给具体的成员函数。

### 2.4 `XProp` — 属性描述

- 描述类中的一个成员变量（属性）。
- 成员：
  - `Name`：属性名字符串。
  - `Type`：属性类型的 `XTypeId`。
  - `Size`：属性大小（`sizeof`）。
  - `Storage`：内嵌存储成员指针（`T Class::*`），大小固定 16 字节（通常足以容纳成员指针）。
  - `GetPtrThunk` / `GetConstPtrThunk`：函数指针，用于从对象指针中获取该属性的地址（可写/只读）。
  - `Meta`：附加元数据（键值对），例如标记“可序列化”、“显示名称”等。
- 提供 `GetValue<T>(obj)` 方法，若类型匹配则返回指向该成员变量的指针（可写或只读）。

**用途**：允许在运行时按名称访问对象的成员变量，例如序列化时遍历属性。

### 2.5 `XFunc` — 函数描述

- 描述类中的一个成员函数。
- 成员：
  - `Name`：函数名字符串。
  - `Thunk`：静态函数指针，负责将参数数组和对象指针转换为实际调用。
  - `Storage`：内嵌存储成员函数指针（`Ret (Class::*)(Args...)`），大小固定 48 字节。
- 提供 `Invoke<Ret>(obj, args...)` 方法：将参数打包为 `XArg` 数组，调用 `Thunk`，并从返回的 `XAny` 中提取结果。

**用途**：允许在运行时按名称调用成员函数，参数和返回值都是动态的。

### 2.6 `XReflex` — 类的反射信息集合

- 存储一个类的所有反射数据：
  - `Classes`：类名列表（包括基类链，用于 `IsA` 判断）。
  - `Props`：属性列表（`XProp`）。
  - `Funcs`：函数列表（`XFunc`）。
- 提供 `AddClass`、`AddProp`、`AddFunc` 等方法用于注册。
- `HasClass` 检查类继承链中是否包含指定类名（用于类型转换判断）。
- `FindFunc` 按名称查找函数。

**用途**：一个 `XReflex` 对象对应一个类的反射元数据，由宏自动填充。

### 2.7 `XClass` / `XClassFactory` / `XSubclass` — 类工厂与多态支持

- **`XClass<Type>`**：抽象基类，定义了反射类的公共接口。
  - 纯虚函数 `New()` 和 `MakeUnique()` 用于创建对象。
  - `GetName()` 返回类名。
  - `GetReflex()` 返回该类的反射信息。
  - `Invoke()` 封装了通过名称调用函数的功能。
  - 子类需要提供这些虚函数的实现。

- **`XClassFactory<T>`**：静态注册表，管理某个根类型的所有派生类的 `XClass` 实例。通过类名字符串查找对应的 `XClass` 指针，从而可以创建对象或获取反射信息。

- **`XSubclass<B, T>`**：模板类，继承自 `XClass<B>`，用于将具体类 `T` 注册到工厂中。它在构造函数中：
  - 设置类名（`T::SClassName()`）。
  - 创建并填充 `XReflex`（调用 `T::RegReflex`）。
  - 将自身注册到 `XClassFactory<B>`。
  - 实现 `New()` 和 `MakeUnique()`。

**用途**：实现基于类名的对象创建（类似简单工厂），以及多态下的反射访问。

### 2.8 宏系统

宏的目的：简化反射注册代码，避免大量重复的模板代码。

- **`BASE_BODY(SELF, SUPER)`**：在类定义中插入必要的静态成员和虚函数。
  - 定义 `SClass()` 静态函数：返回该类的 `XClass` 指针（从工厂获取）。
  - 定义 `SClassName()` 静态函数：返回类名字符串。
  - 声明静态函数 `RegReflex(XReflex&)`，需要在类外定义。
  - 定义类型别名 `Self` 和 `Super`。
- **`DATA_BODY` / `CLASS_BODY`**：两者都包含 `BASE_BODY`，区别是 `CLASS_BODY` 额外定义了虚函数 `GetClass()`，用于多态获取类信息（支持 `IsA` 和 `Cast`）。
- **`XREFLEX(CLASS)`**：在类外开始定义 `CLASS::RegReflex` 函数，并在其中声明一个全局 `XSubclass` 对象（`CLASS##_C`），用于自动注册到工厂。同时调用基类的 `RegReflex` 以继承反射信息。
- **`XREFLEX_END()`**：结束 `RegReflex` 函数定义。
- **`XPROP(PROP)`**：在 `RegReflex` 函数内注册一个属性，自动获取属性名、成员指针、大小。
- **`XPROP_META(PROP, ...)`**：同上，但可以附加元数据。
- **`XFUNC(FUNC)`**：注册一个成员函数。

---

## 3. 使用流程示例

假设我们有一个基类 `Object` 和一个派生类 `Player`，并希望支持反射。

```cpp
// 基类
class Object {
    CLASS_BODY(Object, void)  // 无基类，Super = void
public:
    virtual ~Object() = default;
};

// 派生类
class Player : public Object {
    CLASS_BODY(Player, Object)
public:
    Player() = default;
    int hp = 100;
    std::string name = "Player";

    void SetHp(int value) { hp = value; }
    int GetHp() const { return hp; }
};

// 在 .cpp 中注册反射
XREFLEX(Object)
    // 注册 Object 自己的属性或函数（如果有）
XREFLEX_END()

XREFLEX(Player)
    XPROP(hp);
    XPROP(name);
    XFUNC(SetHp);
    XFUNC(GetHp);
XREFLEX_END()
```

**运行时使用**：

```cpp
// 通过类名获取 XClass 指针
XClass<Object>* playerClass = XClassFactory<Object>::GetClass("Player");
if (playerClass) {
    // 创建对象
    std::unique_ptr<Object> obj = playerClass->MakeUnique();
    
    // 访问属性
    XReflex* reflex = playerClass->GetReflex();
    for (auto& prop : reflex->GetProps()) {
        if (prop.IsType<int>()) {
            int* ptr = prop.GetValue<int>(obj.get());
            std::cout << prop.Name << " = " << *ptr << std::endl;
        }
    }
    
    // 调用函数
    playerClass->Invoke<void>(obj.get(), "SetHp", 50);
    int hp = playerClass->Invoke<int>(obj.get(), "GetHp");
    
    // 类型判断
    if (IsA<Player>(obj.get())) {
        Player* p = Cast<Player>(obj.get());
        // ...
    }
}
```

---

## 4. 关键设计思想

- **类型擦除**：`XAny` 和 `XArg` 使用类型擦除，使框架能处理任意类型。
- **函数指针存储**：成员指针和成员函数指针被直接存储在 `XProp` / `XFunc` 的固定大小缓冲区中，避免额外的堆分配。
- **宏 + 模板**：宏负责生成样板代码，模板负责类型安全。
- **多态支持**：通过 `CLASS_BODY` 注入虚函数 `GetClass()`，使对象能返回自己的 `XClass`，从而支持运行时类型识别（类似 `dynamic_cast` 的替代）。
- **自动注册**：全局 `XSubclass` 对象在程序启动时构造，自动将类注册到工厂，无需手动调用注册函数。

---

## 5. 优缺点

### 优点
- 轻量级：不依赖 RTTI 或外部库。
- 使用简单：仅需几个宏即可完成注册。
- 支持属性和方法的动态访问、调用。
- 支持继承和多态，`IsA` / `Cast` 安全转换。
- 小对象优化，性能较好。

### 缺点与注意事项
- **宏较多**：代码可读性稍差，且宏展开可能产生难以调试的错误。
- **限制**：
  - 只支持成员函数（非静态），且不支持重载函数（同一名称只能注册一个）。
  - 属性访问基于成员指针，无法处理计算属性（如 getter/setter 分开的情况）。
  - `XAny` 不支持拷贝，只支持移动。
  - 类型 ID 基于编译器函数签名，不同编译器可能生成不同 ID（但同一程序内一致）。
- **线程安全**：全局注册表使用 `new` 分配的 map，静态初始化顺序可能存在问题，但作者使用 `new` 避免 SIOF，但并非线程安全（多线程首次访问需注意）。
- **异常安全**：部分代码使用断言而非异常，需确保调用时类型正确。

---
