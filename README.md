# XReflex

**XReflex** 是一个专为现代 C++ (C++17/20) 设计的轻量级、高性能侵入式反射库。它在不需要预处理工具（如 UHT）的情况下，通过宏和模板元编程的结合，提供了一套类似虚幻引擎 (Unreal Engine) 的类型安全反射系统。

## 🌟 核心特性

* **⚡ 高执行效率**：核心依赖编译期类型哈希 (`XTypeId` 基于 `__FUNCSIG__` / `__PRETTY_FUNCTION__` 静态计算)，类型判断为 $O(1)$ 的整数比较。
* **🪶 极致轻量 (Small Object Optimization)**：内建 `XAny` 采用 32 字节栈内存的小对象优化（SOO）。大部分类型在传递和返回时都不会触发堆分配（Heap Allocation）。
* **🚫 无标准 RTTI 依赖**：完全弃用 C++ 标准的 `dynamic_cast` 和 `typeid`，即使在编译参数中关闭 RTTI (`-fno-rtti` / `/GR-`) 也能完美运行。
* **🛡️ 无异常 (No Exceptions)**：内部大量使用 `assert` 与安全检查，内存分配使用 `new (std::nothrow)`，不会抛出任何 C++ 异常，非常适合游戏引擎和实时渲染环境。
* **🎮 虚幻引擎风格 API**：原生支持基于注册表的类型系统、安全的动态向下转型 (`Cast<T>`, `IsA<T>`) 以及类层次结构追踪 (`Super`)。
* **🏷️ 元数据支持**：属性可绑定任意键值对 (`Meta`)，方便实现类似编辑器面板暴露、网络序列化标记等功能。

---

## 🛠️ 安装方法

**XReflex** 是一个 Header-Only 风格的库（虽然你需要在 `.cpp` 中注册）。
只需将 `XReflex.h` 拖入你的工程目录中，并在需要的地方 `#include "XReflex.h"` 即可。

**编译器要求**：要求 C++17 或以上标准（依赖 `<string_view>`, `std::remove_cvref_t`, `if constexpr` 等特性）。

---

## 🚀 示例用法

### 1. 定义类与数据结构

使用 `CLASS_BODY` 定义具有多态性质的类，使用 `DATA_BODY` 定义纯数据结构（不包含虚函数开销）。

```cpp
#include "XReflex.h"

// 定义一个基础多态类（根节点），Super 设为 void
class Object {
    CLASS_BODY(Object, void)
public:
    virtual ~Object() = default;
};

// 定义一个子类
class Character : public Object {
    CLASS_BODY(Character, Object)
public:
    int Health = 100;
    std::string Name = "NPC";

    void TakeDamage(int Damage) {
        Health -= Damage;
        std::cout << Name << " took " << Damage << " damage. HP: " << Health << "\n";
    }
};

// 定义一个纯数据结构
struct Vector3 {
    DATA_BODY(Vector3, void)
    float X = 0.f, Y = 0.f, Z = 0.f;
};

```

### 2. 注册反射信息

通常在 `.cpp` 文件中进行类型的注册。

```cpp
// 注册基类
XREFLEX(Object)
XREFLEX_END()

// 注册子类，暴露属性和方法
XREFLEX(Character)
    XPROP(Health);
    XPROP_META(Name, {"ShowInEditor", "true"}, {"MaxLength", "32"}); // 带元数据的属性
    XFUNC(TakeDamage);
XREFLEX_END()

// 注册数据结构
XREFLEX(Vector3)
    XPROP(X);
    XPROP(Y);
    XPROP(Z);
XREFLEX_END()

```

### 3. 类型识别与安全转换 (Cast / IsA)

```cpp
Object* Obj = Character::SClass()->New(); // 工厂模式创建实例

if (IsA<Character>(Obj)) {
    // 安全转型
    Character* CharObj = Cast<Character>(Obj);
    std::cout << "Successfully casted to Character!" << std::endl;
}

// 释放内存
delete Obj; 

```

### 4. 动态属性访问与方法调用

```cpp
Object* MyChar = Character::SClass()->New();

// 1. 获取并调用反射函数
// XClass<T> 提供了 Invoke 快捷方式，自动处理参数打包解包
Character::SClass()->Invoke<void>(MyChar, "TakeDamage", 20); 

// 2. 访问反射属性
auto* Reflex = MyChar->GetClass()->GetReflex();
for (const auto& Prop : Reflex->GetProps()) {
    if (Prop.Name == "Health") {
        int* HealthPtr = Prop.GetValue<int>(MyChar);
        if (HealthPtr) {
            *HealthPtr = 999; // 动态修改属性
        }
    }
    
    // 读取元数据
    if (Prop.HasMeta("ShowInEditor")) {
        std::cout << Prop.Name << " is visible in editor." << std::endl;
    }
}

```

---

## 🤝 致虚幻引擎 (Unreal Engine) 开发者

如果您习惯了 UE 的反射生态，**XReflex** 的设计逻辑对您来说将非常自然。底层思想完全一致：通过记录类的层次结构和成员偏移量，绕过 C++ 原生的 RTTI 限制。

以下是概念与语法的对应表，帮助您无缝上手：

| 虚幻引擎 (Unreal Engine) | XReflex | 说明 |
| --- | --- | --- |
| `UCLASS()`<br>

<br>`class UMyClass : public UObject`<br>

<br>`{ GENERATED_BODY() ... }` | `class MyClass : public Object`<br>

<br>`{ CLASS_BODY(MyClass, Object) ... }` | 类的声明。XReflex 使用 `CLASS_BODY`，并将父类类型作为第二个参数传入。 |
| `UPROPERTY(EditAnywhere)` | `XPROP_META(VarName, {"EditAnywhere", "1"})` | 属性的注册。UE 通过 UHT 生成，XReflex 需要在 cpp 中用宏显式注册。 |
| `UFUNCTION()` | `XFUNC(FuncName)` | 函数的注册。XReflex 支持变参且自动处理装箱拆箱。 |
| `UMyClass::StaticClass()` | `MyClass::SClass()` | 获取元类 (Metaclass) 单例对象。 |
| `Obj->IsA(UMyClass::StaticClass())`<br>

<br>`Obj->IsA<UMyClass>()` | `IsA<MyClass>(Obj)` | 判断实例是否属于某个类（或其子类）。 |
| `Cast<UMyClass>(Obj)` | `Cast<MyClass>(Obj)` | 安全的动态向下转型。如果失败则返回 `nullptr`。 |
| `Super::BeginPlay()` | `Super::BeginPlay()` | `CLASS_BODY` 内部会自动 `using Super = 父类`，与 UE 用法完全一致！ |
| `UScriptStruct` (纯数据) | `DATA_BODY` 宏 | XReflex 通过 `DATA_BODY` 声明非多态的纯数据反射（无 vtable 开销）。 |

### 核心差异

与 UE 庞大的 `UnrealHeaderTool (UHT)` 不同，**XReflex 不需要外部解析器预编译**。所有的注册工作依靠 `XREFLEX(ClassName)` 到 `XREFLEX_END()` 宏在编译阶段/全局初始化阶段完成，这牺牲了一定的自动便利性，但换取了极轻的接入成本。