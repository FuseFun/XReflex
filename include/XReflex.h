#ifndef XREFLEX_H
#define XREFLEX_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>
#include <string>
#include <tuple>
#include <type_traits>
#include <string_view>
#include <unordered_map>
#include <iostream>

// ---------------------------------------------------------------------------------------------------------------------

template <typename T>
struct XDecayArray
{ 
    using type = T&&; 
};

template <typename T, size_t N>
struct XDecayArray<T(&)[N]>
{ 
    using type = T*; 
};

// ---------------------------------------------------------------------------------------------------------------------

class XTypeId
{
    uint64_t Id{ 0 };

    static constexpr uint64_t HashStr(const char* InString)
    {
        uint64_t hash = 0xcbf29ce484222325ULL;
        while (*InString)
        {
            hash ^= static_cast<uint64_t>(*InString++);
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

public:
    constexpr XTypeId() = default; // for array init
    explicit constexpr XTypeId(uint64_t InId) : Id(InId) {}

    bool operator==(const XTypeId&) const = default;

    template<typename T>
    static constexpr XTypeId Get()
    {
#if defined(_MSC_VER)
        return XTypeId(HashStr(__FUNCSIG__));
#else
        return XTypeId(HashStr(__PRETTY_FUNCTION__));
#endif
    }

    auto Hash() const { return Id; }
};

class XAny
{
    XTypeId Type{ 0 };
    
    void (*Deleter)(void*){ nullptr };
    void (*Mover)(void*, void*){ nullptr };

    // 32bytes SOO
    alignas(std::max_align_t) uint8_t StackStorage[32]{ 0 };

public:
    XAny() = default;

    template<typename T>
    explicit XAny(T&& InValue)
        : Type(XTypeId::Get<std::remove_cvref_t<T>>())
    {
        using CleanT = std::remove_cvref_t<T>;
        constexpr bool bIsInline = sizeof(CleanT) <= sizeof(StackStorage) && alignof(CleanT) <= alignof(decltype(StackStorage));

        if constexpr (bIsInline)
        {
            Deleter = [](void* InStorage)
            {
                std::launder(static_cast<CleanT*>(InStorage))->~CleanT();
            };

            Mover = [](void* InDest, void* InSrc)
            {
                ::new (InDest) CleanT(std::move(*std::launder(static_cast<CleanT*>(InSrc))));
            };

            ::new (StackStorage) CleanT(std::forward<T>(InValue));
        }
        else
        {
            // large obj auto degraded to heap alloc, ptr save to LocalStorage
            Deleter = [](void* InStorage)
            {
                CleanT* Ptr = *static_cast<CleanT**>(InStorage);
                delete Ptr;
            };

            Mover = [](void* InDest, void* InSrc)
            {
                CleanT* srcPtr = *static_cast<CleanT**>(InSrc);
                *static_cast<CleanT**>(InDest) = srcPtr;
            };

            CleanT* Ptr = new CleanT(std::forward<T>(InValue));
            std::memcpy(StackStorage, &Ptr, sizeof(CleanT*));
        }
    }

    ~XAny()
    {
        if (Deleter)
        {
            Deleter(StackStorage);
        }
    }

    XAny(XAny&& InOther) noexcept
    {
        if (InOther.Mover)
        {
            Type = InOther.Type;
            Deleter = InOther.Deleter;
            Mover = InOther.Mover;
            
            Mover(StackStorage, InOther.StackStorage);
            
            InOther.Type = XTypeId{ 0 };
            InOther.Deleter = nullptr;
            InOther.Mover = nullptr;
        }
    }

    XAny& operator=(XAny&& InOther) noexcept
    {
        if (this != &InOther)
        {
            if (Deleter)
            {
                Deleter(StackStorage);
            }

            Type = InOther.Type;
            Deleter = InOther.Deleter;
            Mover = InOther.Mover;

            if (InOther.Mover)
            {
                Mover(StackStorage, InOther.StackStorage);
                
                InOther.Type = XTypeId{ 0 };
                InOther.Deleter = nullptr;
                InOther.Mover = nullptr;
            }
            else
            {
                Type = XTypeId{ 0 };
            }
        }
        return *this;
    }

    XAny(const XAny&) = delete;
    XAny& operator=(const XAny&) = delete;

    bool HasValue() const { return Deleter != nullptr; }

    template<typename T>
    bool Is() const { return Type == XTypeId::Get<T>(); }

    template<typename T>
    T& Get() const
    {
        assert(HasValue() && Is<T>());
        using CleanT = std::remove_cv_t<T>;

        constexpr bool bHeapStorage = sizeof(CleanT) > sizeof(StackStorage) || alignof(CleanT) > alignof(decltype(StackStorage));
        if constexpr (bHeapStorage)
        {
            T* Ptr;
            std::memcpy(&Ptr, StackStorage, sizeof(T*));
            return *Ptr;
        }
        else
        {
            return *std::launder(reinterpret_cast<T*>(const_cast<uint8_t*>(StackStorage)));
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------

struct XArg
{
    XTypeId Type;
    void* Ptr;

    template<typename T>
    T As() const
    {
        using CleanT = std::remove_cvref_t<T>;
        const XTypeId target = XTypeId::Get<CleanT>();

        // fast path
        if (Type == target)
        {
            return *static_cast<CleanT*>(Ptr);
        }

        // str convert
        if (Type == XTypeId::Get<const char*>())
        {
            if constexpr (std::is_same_v<CleanT, std::string_view>)
            {
                return std::string_view(*static_cast<const char* const*>(Ptr));
            }
            if constexpr (std::is_same_v<CleanT, std::string>)
            {
                return std::string(*static_cast<const char* const*>(Ptr));
            }
        }
        
        std::cerr << "XArg::As(): type not match! target type id: " << target.Hash() << "\n";
        assert(false && "XArg::As(): type not match");
        return *static_cast<CleanT*>(Ptr);
    }
};

// ---------------------------------------------------------------------------------------------------------------------

struct XProp
{
    std::string Name;
    XTypeId Type;
    size_t Size{ 0 };

    alignas(void*) uint8_t Storage[16]{ 0 };

    void* (*GetPtrThunk)(const void* InStorage, void* InObj){ nullptr };
    const void* (*GetConstPtrThunk)(const void* InStorage, const void* InObj){ nullptr };

    // meta
    std::unordered_map<std::string, std::string> Meta;

    XProp(std::string InName, XTypeId InTypeIndex, size_t InSize, std::unordered_map<std::string, std::string> InMeta = {})
        : Name(std::move(InName)), Type(InTypeIndex), Size(InSize), Meta(std::move(InMeta))
    {}
    
    bool HasMeta(const std::string& InKey) const { return Meta.find(InKey) != Meta.end(); }
    
    std::string GetMeta(const std::string& InKey) const 
    {
        const auto it = Meta.find(InKey);
        return it != Meta.end() ? it->second : ""; 
    }

    // value
    template<typename P>
    P* GetValue(void* InObj) const
    {
        if (Type == XTypeId::Get<P>() && GetPtrThunk)
        {
            return static_cast<P*>(GetPtrThunk(Storage, InObj));
        }
        return nullptr;
    }
    
    template<typename P>
    const P* GetValue(const void* InObj) const
    {
        if (Type == XTypeId::Get<P>() && GetConstPtrThunk)
        {
            return static_cast<const P*>(GetConstPtrThunk(Storage, InObj));
        }
        return nullptr;
    }

    // type
    template<typename P>
    bool IsType() const { return Type == XTypeId::Get<P>(); }
};

struct XFunc
{
    std::string Name;
    XAny (*Thunk)(void*, void*, const XArg*, size_t){ nullptr };

    alignas(void*) uint8_t Storage[48]{ 0 };

    XFunc(std::string InName, XAny (*InThunk)(void*, void*, const XArg*, size_t))
        : Name(std::move(InName)), Thunk(InThunk)
    {}

    template<typename Ret, typename... Args>
    Ret Invoke(void* InObj, Args&&... InArgs) const
    {
        using ForwardTuple = std::tuple<typename XDecayArray<Args>::type...>;
        ForwardTuple tupleArgs(std::forward<Args>(InArgs)...);

        XAny res;
        constexpr size_t ArgCount = sizeof...(Args);

        if constexpr (ArgCount > 0)
        {
            XArg packedArgs[ArgCount]; 
            size_t index = 0;
            std::apply([&](auto&... args)
            {
                ((packedArgs[index++] = XArg{ XTypeId::Get<std::remove_cvref_t<decltype(args)>>(), (void*)&args }), ...);
            }, tupleArgs);

            res = Thunk((void*)Storage, InObj, packedArgs, ArgCount);
        }
        else
        {
            res = Thunk((void*)Storage, InObj, nullptr, 0);
        }

        if constexpr (!std::is_same_v<Ret, void>)
        {
            return res.Get<Ret>();
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------

struct XReflex
{
    std::vector<const char*> Classes;
    std::vector<XProp> Props;
    std::vector<XFunc> Funcs;

    void AddClass(const char* InClassName) { Classes.push_back(InClassName); }
    
    template<typename Class, typename T>
    void AddProp(std::string InName, T Class::* InPtr, size_t InSize, std::unordered_map<std::string, std::string> InMeta = {}) 
    { 
        XProp prop(std::move(InName), XTypeId::Get<T>(), InSize, std::move(InMeta));
        
        static_assert(sizeof(InPtr) <= sizeof(prop.Storage), "member prop ptr overflow");
        std::memcpy(prop.Storage, &InPtr, sizeof(InPtr));
        
        prop.GetPtrThunk = [](const void* InStorage, void* InObj) -> void*
        {
            auto ptr = *static_cast<const decltype(InPtr)*>(InStorage);
            return &(static_cast<Class*>(InObj)->*ptr);
        };
        
        prop.GetConstPtrThunk = [](const void* InStorage, const void* InObj) -> const void*
        {
            auto ptr = *static_cast<const decltype(InPtr)*>(InStorage);
            return &(static_cast<const Class*>(InObj)->*ptr);
        };

        Props.push_back(std::move(prop));
    }

    template<typename MemberFuncPtr>
    void AddFunc(std::string InName, MemberFuncPtr InFuncPtr)
    {
        XFunc func(std::move(InName), [](void* InStorage, void* InObj, const XArg* InArgs, size_t InArgCount) -> XAny
        {
            MemberFuncPtr actualF = *static_cast<MemberFuncPtr*>(InStorage);
            return CallHelper(InObj, actualF, InArgs, InArgCount);
        });

        static_assert(sizeof(MemberFuncPtr) <= sizeof(func.Storage), "member func ptr overflow");
        std::memcpy(func.Storage, &InFuncPtr, sizeof(MemberFuncPtr));

        Funcs.push_back(std::move(func));
    }

    bool HasClass(std::string_view InClassName) const
    {
        for (auto ClassName : Classes)
        {
            if (ClassName == InClassName)
            {
                return true;
            }
        }
        return false;
    }

    const XFunc* FindFunc(const std::string_view InName) const
    {
        for (const auto& func : Funcs)
        {
            if (func.Name == InName)
            {
                return &func;
            }
        }
        return nullptr;
    }

    const std::vector<XProp>& GetProps() const { return Props; }
    const std::vector<XFunc>& GetFuncs() const { return Funcs; }

private:
    // default func helper
    template<typename Ret, typename Class, typename... Params>
    static XAny CallHelper(void* InObj, Ret(Class::*InFuncPtr)(Params...), const XArg* InArgs, size_t InArgCount)
    {
        assert(InArgCount == sizeof...(Params));
        return InvokeHelper<Ret, Class, Params...>(InObj, InFuncPtr, InArgs, std::index_sequence_for<Params...>{});
    }

    template<typename Ret, typename Class, typename... Params, size_t... Is>
    static XAny InvokeHelper(void* InObj, Ret(Class::*InFuncPtr)(Params...), const XArg* InArgs, std::index_sequence<Is...>)
    {
        if constexpr (std::is_same_v<Ret, void>)
        {
            (static_cast<Class*>(InObj)->*InFuncPtr)(InArgs[Is].template As<Params>()...);
            return XAny();
        }
        else
        {
            return XAny((static_cast<Class*>(InObj)->*InFuncPtr)(InArgs[Is].template As<Params>()...));
        }
    }

    // const func helper
    template<typename Ret, typename Class, typename... Params>
    static XAny CallHelper(void* InObj, Ret(Class::*InFuncPtr)(Params...) const, const XArg* InArgs, size_t InArgCount)
    {
        assert(InArgCount == sizeof...(Params));
        return InvokeHelper<Ret, Class, Params...>(InObj, InFuncPtr, InArgs, std::index_sequence_for<Params...>{});
    }

    template<typename Ret, typename Class, typename... Params, size_t... Is>
    static XAny InvokeHelper(void* InObj, Ret(Class::*InFuncPtr)(Params...) const, const XArg* InArgs, std::index_sequence<Is...>)
    {
        if constexpr (std::is_same_v<Ret, void>)
        {
            (static_cast<const Class*>(InObj)->*InFuncPtr)(InArgs[Is].template As<Params>()...);
            return XAny();
        }
        else
        {
            return XAny((static_cast<const Class*>(InObj)->*InFuncPtr)(InArgs[Is].template As<Params>()...));
        }
    }
};

namespace XReflexInternal
{
    template<typename T>
    void CallRegReflex(XReflex& r) { if constexpr (!std::is_same_v<T, void>) T::RegReflex(r); }
}

// ---------------------------------------------------------------------------------------------------------------------

template<typename Type>
class XClass
{
public:
    virtual const char* GetName() const { return Name; }
    virtual Type* New() = 0;
    virtual std::unique_ptr<Type> MakeUnique() = 0;
    XReflex* GetReflex() const { return Reflex.get(); }

    template<typename Ret, typename... Args>
    Ret Invoke(void* InObj, const std::string_view InFuncName, Args&&... InArgs) const
    {
        auto* func = Reflex->FindFunc(InFuncName);
        assert(func);
        return func->template Invoke<Ret>(InObj, std::forward<Args>(InArgs)...);
    }

protected:
    virtual ~XClass() = default;
    const char* Name{ nullptr };
    std::unique_ptr<XReflex> Reflex{};
};

template<typename T, typename = std::enable_if_t<std::is_class_v<T>>>
class XClassFactory
{
public:
    static XClass<T>* GetClass(const char* InClassName)
    {
        auto& classesMap = GetClassesMap();
        auto it = classesMap.find(std::string_view(InClassName)); 
        return it != classesMap.end() ? it->second : nullptr;
    }
    
    static void RegClass(XClass<T>* InClassInstance)
    {
        GetClassesMap()[InClassInstance->GetName()] = InClassInstance;
    }

private:
    // never delete, to prevent SIOF
    static std::unordered_map<std::string_view, XClass<T>*>& GetClassesMap()
    {
        static auto* ClassesMap = new std::unordered_map<std::string_view, XClass<T>*>();
        return *ClassesMap;
    }
};

template<typename B, typename T>
class XSubclass : public XClass<B>
{
public:
    XSubclass()
    {
        this->Name = T::SClassName();
        this->Reflex = std::make_unique<XReflex>();
        T::RegReflex(*(this->Reflex));
        XClassFactory<B>::RegClass(this);
    }

    B* New() override { return new (std::nothrow) T(); }
    std::unique_ptr<B> MakeUnique() override { return std::make_unique<T>(); }
};

// ---------------------------------------------------------------------------------------------------------------------

template <typename T, typename = void> struct GetRootClass { using type = T; };
template <typename T> struct GetRootClass<T, std::void_t<typename T::Super>>
{
    using type = std::conditional_t<std::is_same_v<typename T::Super, void>, T, typename GetRootClass<typename T::Super>::type>;
};

template <typename Self, typename Super>
struct XRootClassHelper
{
    using type = std::conditional_t<std::is_same_v<Super, void>, Self, typename GetRootClass<Super>::type>;
};

template <typename To, typename From>
bool IsA(const From* InSrc)
{
    if (!InSrc)
        return false;
    
    auto* classInstance = InSrc->GetClass();
    if (!classInstance)
        return false;
    
    return classInstance->GetReflex()->HasClass(To::SClassName());
}

template <typename To, typename From>
auto Cast(From* InSrc) -> std::conditional_t<std::is_const_v<From>, const To*, To*>
{
    return IsA<To>(InSrc) ? static_cast<std::conditional_t<std::is_const_v<From>, const To*, To*>>(InSrc) : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
#define BASE_BODY(SELF, SUPER) \
public: \
static XClass<typename XRootClassHelper<SELF, SUPER>::type>* SClass() \
{ \
return XClassFactory<typename XRootClassHelper<SELF, SUPER>::type>::GetClass(SClassName()); \
} \
static constexpr const char* SClassName() { return #SELF; } \
static void RegReflex(XReflex& reflex); \
using Self = SELF; \
using Super = SUPER;

// DATA_BODY: pure data struct/class (e.g. Vector3, Color ...)
#define DATA_BODY(SELF, SUPER) \
BASE_BODY(SELF, SUPER) \
public:

// CLASS_BODY: support poly struct/class (e.g. Player, Monster ...)
#define CLASS_BODY(SELF, SUPER) \
BASE_BODY(SELF, SUPER) \
public: \
virtual XClass<typename XRootClassHelper<SELF, SUPER>::type>* GetClass() const \
{ \
return SClass(); \
} \
private:

// ---------------------------------------------------------------------------------------------------------------------

#define XREFLEX(CLASS) \
    XSubclass<typename GetRootClass<CLASS>::type, CLASS> CLASS##_C; \
    void CLASS::RegReflex(XReflex& reflex) { \
        XReflexInternal::CallRegReflex<CLASS::Super>(reflex); \
        reflex.AddClass(CLASS::SClassName());

#define XREFLEX_END() }

#define XPROP(PROP) reflex.AddProp(#PROP, &Self::PROP, sizeof(decltype(Self::PROP)))
#define XPROP_META(PROP, ...) reflex.AddProp(#PROP, &Self::PROP, sizeof(decltype(Self::PROP)), {__VA_ARGS__})

#define XFUNC(FUNC) reflex.AddFunc(#FUNC, &Self::FUNC)

// ---------------------------------------------------------------------------------------------------------------------


#endif //XREFLEX_H