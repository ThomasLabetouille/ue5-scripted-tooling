// ============================================================================
// Doublures minimales d'Unreal, pour compiler BlockoutPanelGeometry.cpp HORS
// MOTEUR et exercer ses proprietes geometriques en quelques secondes.
//
// Portage de l'idee de Tools/GeometryTests/UnityShims.cs (projet Unity
// LevelDesignTools) : on compile LE FICHIER DU PROJET, pas une copie, et les
// doublures sont le garde-fou -- si la geometrie se met a utiliser un type
// Unreal absent d'ici, ce harnais ne compile plus et on le sait tout de suite.
//
// Ces doublures reproduisent volontairement les pieges du vrai moteur :
//   - PI est une MACRO (c'est ce qui a casse le premier build) ;
//   - FVector2D est en DOUBLE et FVector3f/FVector2f en FLOAT, comme UE5.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

using int32  = std::int32_t;
using uint32 = std::uint32_t;
using int64  = std::int64_t;
using uint64 = std::uint64_t;
using uint8  = std::uint8_t;

#define BLOCKOUTTOOLS_API
#define PI (3.1415926535897932f)   // macro, exactement comme UnrealMathUtility.h

template <typename T> void Swap(T& A, T& B) { T Tmp = std::move(A); A = std::move(B); B = std::move(Tmp); }
template <typename T> T&& MoveTemp(T& V) { return std::move(V); }

template <typename T>
struct TNumericLimits
{
    static constexpr T Max()    { return std::numeric_limits<T>::max(); }
    static constexpr T Lowest() { return std::numeric_limits<T>::lowest(); }
    static constexpr T Min()    { return std::numeric_limits<T>::min(); }
};

// ── FMath ───────────────────────────────────────────────────────────────────
struct FMath
{
    template <typename T> static T Abs(T V) { return V < T(0) ? -V : V; }
    template <typename A, typename B> static auto Min(A a, B b) -> decltype(a + b) { return a < b ? a : b; }
    template <typename A, typename B> static auto Max(A a, B b) -> decltype(a + b) { return a > b ? a : b; }
    template <typename T> static T Clamp(T V, T Lo, T Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
    static double RoundToDouble(double V) { return std::round(V); }
    static float  Sqrt(float V)  { return std::sqrt(V); }
    static double Sqrt(double V) { return std::sqrt(V); }
};

// ── Vecteurs ────────────────────────────────────────────────────────────────
struct FVector2D
{
    double X = 0.0, Y = 0.0;
    FVector2D() = default;
    FVector2D(double InX, double InY) : X(InX), Y(InY) {}

    static const FVector2D ZeroVector;
    static double Distance(const FVector2D& A, const FVector2D& B)
    {
        const double DX = B.X - A.X, DY = B.Y - A.Y;
        return std::sqrt(DX * DX + DY * DY);
    }
    bool operator==(const FVector2D& O) const { return X == O.X && Y == O.Y; }
    bool operator!=(const FVector2D& O) const { return !(*this == O); }
};

struct FVector2f
{
    float X = 0.f, Y = 0.f;
    FVector2f() = default;
    FVector2f(float InX, float InY) : X(InX), Y(InY) {}
    explicit FVector2f(const FVector2D& V) : X((float)V.X), Y((float)V.Y) {}
    static const FVector2f ZeroVector;
};

struct FVector3f
{
    float X = 0.f, Y = 0.f, Z = 0.f;
    FVector3f() = default;
    FVector3f(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

    static const FVector3f ZeroVector;

    float& operator[](int32 i)             { return i == 0 ? X : (i == 1 ? Y : Z); }
    const float& operator[](int32 i) const { return i == 0 ? X : (i == 1 ? Y : Z); }

    FVector3f operator-(const FVector3f& O) const { return FVector3f(X - O.X, Y - O.Y, Z - O.Z); }
    FVector3f operator+(const FVector3f& O) const { return FVector3f(X + O.X, Y + O.Y, Z + O.Z); }

    static FVector3f CrossProduct(const FVector3f& A, const FVector3f& B)
    {
        return FVector3f(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X);
    }
    float SizeSquared() const { return X * X + Y * Y + Z * Z; }
    FVector3f GetSafeNormal(float Tolerance = 1.e-8f) const
    {
        const float S = SizeSquared();
        if (S < Tolerance) return ZeroVector;
        const float Inv = 1.f / std::sqrt(S);
        return FVector3f(X * Inv, Y * Inv, Z * Inv);
    }
    bool IsNearlyZero(float Tolerance = 1.e-4f) const
    {
        return std::fabs(X) <= Tolerance && std::fabs(Y) <= Tolerance && std::fabs(Z) <= Tolerance;
    }
};

struct FVector
{
    double X = 0.0, Y = 0.0, Z = 0.0;
    FVector() = default;
    FVector(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

    double& operator[](int32 i)             { return i == 0 ? X : (i == 1 ? Y : Z); }
    const double& operator[](int32 i) const { return i == 0 ? X : (i == 1 ? Y : Z); }
};

inline uint32 GetTypeHash(const FVector2D& V)
{
    uint64 BitsX, BitsY;
    std::memcpy(&BitsX, &V.X, sizeof(BitsX));
    std::memcpy(&BitsY, &V.Y, sizeof(BitsY));
    const uint64 Mixed = BitsX * 1099511628211ull ^ BitsY;
    return (uint32)(Mixed ^ (Mixed >> 32));
}
inline uint32 GetTypeHash(uint64 V) { return (uint32)(V ^ (V >> 32)); }
inline uint32 GetTypeHash(int32 V)  { return (uint32)V; }

// ── TArray ──────────────────────────────────────────────────────────────────
template <typename T>
class TArray
{
public:
    int32 Num() const { return (int32)Items.size(); }
    bool IsEmpty() const { return Items.empty(); }
    void Reset()   { Items.clear(); }
    void Empty()   { Items.clear(); }
    void Reserve(int32 N) { Items.reserve((size_t)N); }
    void SetNumUninitialized(int32 N) { Items.resize((size_t)N); }
    bool IsValidIndex(int32 i) const { return i >= 0 && i < Num(); }

    int32 Add(const T& V) { Items.push_back(V); return Num() - 1; }
    int32 Add(T&& V)      { Items.push_back(std::move(V)); return Num() - 1; }

    T& Last(int32 Back = 0)             { return Items[Items.size() - 1 - (size_t)Back]; }
    const T& Last(int32 Back = 0) const { return Items[Items.size() - 1 - (size_t)Back]; }
    T Pop() { T V = Items.back(); Items.pop_back(); return V; }
    void RemoveAt(int32 i) { Items.erase(Items.begin() + i); }

    T& operator[](int32 i)             { return Items[(size_t)i]; }
    const T& operator[](int32 i) const { return Items[(size_t)i]; }

    void Sort() { std::stable_sort(Items.begin(), Items.end()); }
    template <typename P> void Sort(P Pred) { std::stable_sort(Items.begin(), Items.end(), Pred); }

    auto begin()       { return Items.begin(); }
    auto end()         { return Items.end(); }
    auto begin() const { return Items.begin(); }
    auto end()   const { return Items.end(); }

private:
    std::vector<T> Items;
};

// ── TMap ────────────────────────────────────────────────────────────────────
template <typename K, typename V>
struct TPair
{
    K Key;
    V Value;
};

template <typename K, typename V>
class TMap
{
    struct FHasher { size_t operator()(const K& Key) const { return (size_t)GetTypeHash(Key); } };

public:
    V* Find(const K& Key)
    {
        auto It = Index.find(Key);
        return It == Index.end() ? nullptr : &Entries[It->second].Value;
    }
    const V* Find(const K& Key) const
    {
        auto It = Index.find(Key);
        return It == Index.end() ? nullptr : &Entries[It->second].Value;
    }
    V& FindChecked(const K& Key) { return Entries[Index.at(Key)].Value; }
    V& Add(const K& Key, const V& Value)
    {
        auto It = Index.find(Key);
        if (It != Index.end()) { Entries[It->second].Value = Value; return Entries[It->second].Value; }
        Index.emplace(Key, Entries.size());
        Entries.push_back(TPair<K, V>{Key, Value});
        return Entries.back().Value;
    }
    V& FindOrAdd(const K& Key)
    {
        auto It = Index.find(Key);
        if (It != Index.end()) return Entries[It->second].Value;
        Index.emplace(Key, Entries.size());
        Entries.push_back(TPair<K, V>{Key, V()});
        return Entries.back().Value;
    }
    bool Contains(const K& Key) const { return Index.find(Key) != Index.end(); }
    int32 Num() const { return (int32)Entries.size(); }
    void GetKeys(TArray<K>& Out) const { for (const auto& E : Entries) Out.Add(E.Key); }

    auto begin() const { return Entries.begin(); }
    auto end()   const { return Entries.end(); }

private:
    std::deque<TPair<K, V>> Entries;               // deque : references stables
    std::unordered_map<K, size_t, FHasher> Index;
};
