using System;
using System.Collections;
using System.Collections.Generic;
using Axiom.Interop;

namespace Axiom;
public class Scene
{
    private delegate int QueryExecutor(Span<ulong> buffer);

    public string Name { get; internal set; } = "";
    public bool IsLoaded
    {
        get
        {
            if (string.IsNullOrEmpty(Name))
                return false;

            int count = InternalCalls.Scene_GetLoadedCount();
            for (int i = 0; i < count; i++)
            {
                if (string.Equals(InternalCalls.Scene_GetLoadedSceneNameAt(i), Name, StringComparison.Ordinal))
                    return true;
            }

            return false;
        }
    }

    // ── Query entry points (1-4 type params) ───────────────────

    public QueryBuilder<T1> Query<T1>()
        where T1 : Component, new()
        => new QueryBuilder<T1>(Name);

    public QueryBuilder<T1, T2> Query<T1, T2>()
        where T1 : Component, new()
        where T2 : Component, new()
        => new QueryBuilder<T1, T2>(Name);

    public QueryBuilder<T1, T2, T3> Query<T1, T2, T3>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        => new QueryBuilder<T1, T2, T3>(Name);

    public QueryBuilder<T1, T2, T3, T4> Query<T1, T2, T3, T4>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        => new QueryBuilder<T1, T2, T3, T4>(Name);

    public QueryBuilder<T1, T2, T3, T4, T5> Query<T1, T2, T3, T4, T5>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        => new QueryBuilder<T1, T2, T3, T4, T5>(Name);

    public QueryBuilder<T1, T2, T3, T4, T5, T6> Query<T1, T2, T3, T4, T5, T6>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        => new QueryBuilder<T1, T2, T3, T4, T5, T6>(Name);

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> Query<T1, T2, T3, T4, T5, T6, T7>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        where T7 : Component, new()
        => new QueryBuilder<T1, T2, T3, T4, T5, T6, T7>(Name);

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> Query<T1, T2, T3, T4, T5, T6, T7, T8>()
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        where T7 : Component, new()
        where T8 : Component, new()
        => new QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8>(Name);

    public int EntityCount => IsLoaded ? InternalCalls.Scene_GetEntityCount(Name) : 0;

    public bool EnableSystem<T>() where T : GameSystem
        => SetSystemEnabled<T>(true);

    public bool DisableSystem<T>() where T : GameSystem
        => SetSystemEnabled<T>(false);

    private bool SetSystemEnabled<T>(bool enabled) where T : GameSystem
    {
        if (string.IsNullOrEmpty(Name))
            return false;

        return InternalCalls.Scene_SetGameSystemEnabled(Name, typeof(T).Name, enabled);
    }

    // ── Internal helpers (static) ──────────────────────────────

    internal static string? GetNativeName<T>() where T : Component, new()
        => Entity.GetNativeComponentName<T>();

    internal static string GetNativeNameOrEmpty<T>() where T : Component, new()
        => Entity.GetNativeComponentName<T>() ?? "";

    internal static ulong[] ExecuteQuery(string sceneName, string queryString)
    {
        if (string.IsNullOrEmpty(sceneName) || string.IsNullOrEmpty(queryString))
            return Array.Empty<ulong>();

        return ExecuteQueryWithRetry(buffer => InternalCalls.Scene_QueryEntities(sceneName, queryString, buffer));
    }


    internal static ulong[] ExecuteFilteredQuery(
        string sceneName, string withComponents, string withoutComponents,
        string mustHaveComponents, int enableFilter)
    {
        if (string.IsNullOrEmpty(sceneName) || string.IsNullOrEmpty(withComponents))
            return Array.Empty<ulong>();

        return ExecuteQueryWithRetry(buffer => InternalCalls.Scene_QueryEntitiesFiltered(
            sceneName, withComponents, withoutComponents, mustHaveComponents,
            enableFilter, buffer));
    }

    private static ulong[] ExecuteQueryWithRetry(QueryExecutor nativeQuery)
    {
        int capacity = Math.Max(64, InternalCalls.Scene_GetEntityCount());
        ulong[] buffer = new ulong[capacity];

        while (true)
        {
            int count = nativeQuery(buffer);
            if (count <= 0)
                return Array.Empty<ulong>();

            if (count <= buffer.Length)
            {
                if (count == buffer.Length)
                    return buffer;

                ulong[] result = new ulong[count];
                Array.Copy(buffer, result, count);
                return result;
            }

            buffer = new ulong[count];
        }
    }

    internal static bool TryGetComponents<T1>(Entity entity, out T1? c1)
        where T1 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        return c1 != null;
    }

    internal static bool TryGetComponents<T1, T2>(Entity entity, out T1? c1, out T2? c2)
        where T1 : Component, new()
        where T2 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        c2 = entity.GetComponent<T2>();
        return c1 != null && c2 != null;
    }

    internal static bool TryGetComponents<T1, T2, T3>(Entity entity, out T1? c1, out T2? c2, out T3? c3)
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        c2 = entity.GetComponent<T2>();
        c3 = entity.GetComponent<T3>();
        return c1 != null && c2 != null && c3 != null;
    }

    internal static bool TryGetComponents<T1, T2, T3, T4>(Entity entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4)
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        c2 = entity.GetComponent<T2>();
        c3 = entity.GetComponent<T3>();
        c4 = entity.GetComponent<T4>();
        return c1 != null && c2 != null && c3 != null && c4 != null;
    }

    internal static bool TryGetComponents<T1, T2, T3, T4, T5>(Entity entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5)
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        c2 = entity.GetComponent<T2>();
        c3 = entity.GetComponent<T3>();
        c4 = entity.GetComponent<T4>();
        c5 = entity.GetComponent<T5>();
        return c1 != null && c2 != null && c3 != null && c4 != null && c5 != null;
    }

    internal static bool TryGetComponents<T1, T2, T3, T4, T5, T6>(Entity entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6)
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        c2 = entity.GetComponent<T2>();
        c3 = entity.GetComponent<T3>();
        c4 = entity.GetComponent<T4>();
        c5 = entity.GetComponent<T5>();
        c6 = entity.GetComponent<T6>();
        return c1 != null && c2 != null && c3 != null && c4 != null && c5 != null && c6 != null;
    }

    internal static bool TryGetComponents<T1, T2, T3, T4, T5, T6, T7>(Entity entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6, out T7? c7)
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        where T7 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        c2 = entity.GetComponent<T2>();
        c3 = entity.GetComponent<T3>();
        c4 = entity.GetComponent<T4>();
        c5 = entity.GetComponent<T5>();
        c6 = entity.GetComponent<T6>();
        c7 = entity.GetComponent<T7>();
        return c1 != null && c2 != null && c3 != null && c4 != null && c5 != null && c6 != null && c7 != null;
    }

    internal static bool TryGetComponents<T1, T2, T3, T4, T5, T6, T7, T8>(Entity entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6, out T7? c7, out T8? c8)
        where T1 : Component, new()
        where T2 : Component, new()
        where T3 : Component, new()
        where T4 : Component, new()
        where T5 : Component, new()
        where T6 : Component, new()
        where T7 : Component, new()
        where T8 : Component, new()
    {
        c1 = entity.GetComponent<T1>();
        c2 = entity.GetComponent<T2>();
        c3 = entity.GetComponent<T3>();
        c4 = entity.GetComponent<T4>();
        c5 = entity.GetComponent<T5>();
        c6 = entity.GetComponent<T6>();
        c7 = entity.GetComponent<T7>();
        c8 = entity.GetComponent<T8>();
        return c1 != null && c2 != null && c3 != null && c4 != null && c5 != null && c6 != null && c7 != null && c8 != null;
    }

    internal static string BuildQueryString(params string?[] names)
    {
        for (int i = 0; i < names.Length; i++)
            if (names[i] == null) return "";
        return string.Join('|', names!);
    }
}

// ── Query Filter State ──────────────────────────────────────────

internal struct QueryFilter
{
    internal string SceneName;
    internal string WithNames;
    internal string WithoutNames;
    internal string MustHaveNames;
    internal int EnableFilter; // 0=all, 1=enabled, 2=disabled
    internal List<Func<Entity, bool>>? Conditions;

    internal QueryFilter(string sceneName, string withNames)
    {
        SceneName = sceneName;
        WithNames = withNames;
        WithoutNames = "";
        MustHaveNames = "";
        EnableFilter = 0;
        Conditions = null;
    }

    internal void AppendWithout(string name)
    {
        if (string.IsNullOrEmpty(name)) return;
        WithoutNames = string.IsNullOrEmpty(WithoutNames) ? name : WithoutNames + "|" + name;
    }

    internal void AppendMustHave(string name)
    {
        if (string.IsNullOrEmpty(name)) return;
        MustHaveNames = string.IsNullOrEmpty(MustHaveNames) ? name : MustHaveNames + "|" + name;
    }

    internal void AddCondition(Func<Entity, bool> condition)
    {
        Conditions ??= new List<Func<Entity, bool>>();
        Conditions.Add(condition);
    }

    internal ulong[] Execute()
    {
        return Scene.ExecuteFilteredQuery(SceneName, WithNames, WithoutNames, MustHaveNames, EnableFilter);
    }

    internal bool PassesConditions(Entity entity)
    {
        if (Conditions == null) return true;
        foreach (var cond in Conditions)
            if (!cond(entity)) return false;
        return true;
    }
}

// ════════════════════════════════════════════════════════════════
//  QueryBuilder<T1>
// ════════════════════════════════════════════════════════════════

public struct QueryBuilder<T1> : IEnumerable<T1>
    where T1 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.GetNativeNameOrEmpty<T1>());
    }

    // ── Filters ────────────────────────────────────────────────

    public QueryBuilder<T1> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1> WithCondition(Func<T1, bool> predicate)
    {
        _filter.AddCondition(e => { var c = e.GetComponent<T1>(); return c != null && predicate(c); });
        return this;
    }

    // ── Terminal operations ─────────────────────────────────────

    public EntityQueryResult<T1> WithEntity() => new EntityQueryResult<T1>(_filter);

    public IEnumerator<T1> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1)) continue;
            yield return c1!;
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1> : IEnumerable<(Entity Entity, T1 C1)>
    where T1 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1)) continue;
            yield return (entity, c1!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

// ════════════════════════════════════════════════════════════════
//  QueryBuilder<T1, T2>
// ════════════════════════════════════════════════════════════════

public struct QueryBuilder<T1, T2> : IEnumerable<(T1 C1, T2 C2)>
    where T1 : Component, new()
    where T2 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.BuildQueryString(
            Scene.GetNativeName<T1>(), Scene.GetNativeName<T2>()));
    }

    public QueryBuilder<T1, T2> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1, T2> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1, T2> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1, T2> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1, T2> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1, T2> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1, T2> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1, T2> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1, T2> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1, T2> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1, T2> WithCondition<TC>(Func<TC, bool> predicate) where TC : Component, new()
    {
        _filter.AddCondition(e => { var c = e.GetComponent<TC>(); return c != null && predicate(c); });
        return this;
    }

    public EntityQueryResult<T1, T2> WithEntity() => new EntityQueryResult<T1, T2>(_filter);

    public IEnumerator<(T1, T2)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2)) continue;
            yield return (c1!, c2!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1, T2> : IEnumerable<(Entity Entity, T1 C1, T2 C2)>
    where T1 : Component, new()
    where T2 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1, T2)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2)) continue;
            yield return (entity, c1!, c2!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

// ════════════════════════════════════════════════════════════════
//  QueryBuilder<T1, T2, T3>
// ════════════════════════════════════════════════════════════════

public struct QueryBuilder<T1, T2, T3> : IEnumerable<(T1 C1, T2 C2, T3 C3)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.BuildQueryString(
            Scene.GetNativeName<T1>(), Scene.GetNativeName<T2>(), Scene.GetNativeName<T3>()));
    }

    public QueryBuilder<T1, T2, T3> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1, T2, T3> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1, T2, T3> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1, T2, T3> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1, T2, T3> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1, T2, T3> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1, T2, T3> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1, T2, T3> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1, T2, T3> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1, T2, T3> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1, T2, T3> WithCondition<TC>(Func<TC, bool> predicate) where TC : Component, new()
    {
        _filter.AddCondition(e => { var c = e.GetComponent<TC>(); return c != null && predicate(c); });
        return this;
    }

    public EntityQueryResult<T1, T2, T3> WithEntity() => new EntityQueryResult<T1, T2, T3>(_filter);

    public IEnumerator<(T1, T2, T3)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3)) continue;
            yield return (c1!, c2!, c3!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1, T2, T3> : IEnumerable<(Entity Entity, T1 C1, T2 C2, T3 C3)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1, T2, T3)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3)) continue;
            yield return (entity, c1!, c2!, c3!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

// ════════════════════════════════════════════════════════════════
//  QueryBuilder<T1, T2, T3, T4>
// ════════════════════════════════════════════════════════════════

public struct QueryBuilder<T1, T2, T3, T4> : IEnumerable<(T1 C1, T2 C2, T3 C3, T4 C4)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.BuildQueryString(
            Scene.GetNativeName<T1>(), Scene.GetNativeName<T2>(),
            Scene.GetNativeName<T3>(), Scene.GetNativeName<T4>()));
    }

    public QueryBuilder<T1, T2, T3, T4> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1, T2, T3, T4> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1, T2, T3, T4> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1, T2, T3, T4> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1, T2, T3, T4> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1, T2, T3, T4> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1, T2, T3, T4> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1, T2, T3, T4> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1, T2, T3, T4> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1, T2, T3, T4> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1, T2, T3, T4> WithCondition<TC>(Func<TC, bool> predicate) where TC : Component, new()
    {
        _filter.AddCondition(e => { var c = e.GetComponent<TC>(); return c != null && predicate(c); });
        return this;
    }

    public EntityQueryResult<T1, T2, T3, T4> WithEntity() => new EntityQueryResult<T1, T2, T3, T4>(_filter);

    public IEnumerator<(T1, T2, T3, T4)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4)) continue;
            yield return (c1!, c2!, c3!, c4!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1, T2, T3, T4> : IEnumerable<(Entity Entity, T1 C1, T2 C2, T3 C3, T4 C4)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1, T2, T3, T4)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4)) continue;
            yield return (entity, c1!, c2!, c3!, c4!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

// QueryBuilder<T1, T2, T3, T4, T5>
public struct QueryBuilder<T1, T2, T3, T4, T5> : IEnumerable<(T1 C1, T2 C2, T3 C3, T4 C4, T5 C5)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.BuildQueryString(
            Scene.GetNativeName<T1>(), Scene.GetNativeName<T2>(),
            Scene.GetNativeName<T3>(), Scene.GetNativeName<T4>(),
            Scene.GetNativeName<T5>()));
    }

    public QueryBuilder<T1, T2, T3, T4, T5> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1, T2, T3, T4, T5> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1, T2, T3, T4, T5> WithCondition<TC>(Func<TC, bool> predicate) where TC : Component, new()
    {
        _filter.AddCondition(e => { var c = e.GetComponent<TC>(); return c != null && predicate(c); });
        return this;
    }

    public EntityQueryResult<T1, T2, T3, T4, T5> WithEntity() => new EntityQueryResult<T1, T2, T3, T4, T5>(_filter);

    public IEnumerator<(T1, T2, T3, T4, T5)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5)) continue;
            yield return (c1!, c2!, c3!, c4!, c5!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1, T2, T3, T4, T5> : IEnumerable<(Entity Entity, T1 C1, T2 C2, T3 C3, T4 C4, T5 C5)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1, T2, T3, T4, T5)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5)) continue;
            yield return (entity, c1!, c2!, c3!, c4!, c5!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

// QueryBuilder<T1, T2, T3, T4, T5, T6>
public struct QueryBuilder<T1, T2, T3, T4, T5, T6> : IEnumerable<(T1 C1, T2 C2, T3 C3, T4 C4, T5 C5, T6 C6)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
    where T6 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.BuildQueryString(
            Scene.GetNativeName<T1>(), Scene.GetNativeName<T2>(),
            Scene.GetNativeName<T3>(), Scene.GetNativeName<T4>(),
            Scene.GetNativeName<T5>(), Scene.GetNativeName<T6>()));
    }

    public QueryBuilder<T1, T2, T3, T4, T5, T6> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5, T6> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5, T6> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1, T2, T3, T4, T5, T6> WithCondition<TC>(Func<TC, bool> predicate) where TC : Component, new()
    {
        _filter.AddCondition(e => { var c = e.GetComponent<TC>(); return c != null && predicate(c); });
        return this;
    }

    public EntityQueryResult<T1, T2, T3, T4, T5, T6> WithEntity() => new EntityQueryResult<T1, T2, T3, T4, T5, T6>(_filter);

    public IEnumerator<(T1, T2, T3, T4, T5, T6)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6)) continue;
            yield return (c1!, c2!, c3!, c4!, c5!, c6!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1, T2, T3, T4, T5, T6> : IEnumerable<(Entity Entity, T1 C1, T2 C2, T3 C3, T4 C4, T5 C5, T6 C6)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
    where T6 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1, T2, T3, T4, T5, T6)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6)) continue;
            yield return (entity, c1!, c2!, c3!, c4!, c5!, c6!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

// QueryBuilder<T1, T2, T3, T4, T5, T6, T7>
public struct QueryBuilder<T1, T2, T3, T4, T5, T6, T7> : IEnumerable<(T1 C1, T2 C2, T3 C3, T4 C4, T5 C5, T6 C6, T7 C7)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
    where T6 : Component, new()
    where T7 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.BuildQueryString(
            Scene.GetNativeName<T1>(), Scene.GetNativeName<T2>(),
            Scene.GetNativeName<T3>(), Scene.GetNativeName<T4>(),
            Scene.GetNativeName<T5>(), Scene.GetNativeName<T6>(),
            Scene.GetNativeName<T7>()));
    }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7> WithCondition<TC>(Func<TC, bool> predicate) where TC : Component, new()
    {
        _filter.AddCondition(e => { var c = e.GetComponent<TC>(); return c != null && predicate(c); });
        return this;
    }

    public EntityQueryResult<T1, T2, T3, T4, T5, T6, T7> WithEntity() => new EntityQueryResult<T1, T2, T3, T4, T5, T6, T7>(_filter);

    public IEnumerator<(T1, T2, T3, T4, T5, T6, T7)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6, out T7? c7)) continue;
            yield return (c1!, c2!, c3!, c4!, c5!, c6!, c7!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1, T2, T3, T4, T5, T6, T7> : IEnumerable<(Entity Entity, T1 C1, T2 C2, T3 C3, T4 C4, T5 C5, T6 C6, T7 C7)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
    where T6 : Component, new()
    where T7 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1, T2, T3, T4, T5, T6, T7)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6, out T7? c7)) continue;
            yield return (entity, c1!, c2!, c3!, c4!, c5!, c6!, c7!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

// QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8>
public struct QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> : IEnumerable<(T1 C1, T2 C2, T3 C3, T4 C4, T5 C5, T6 C6, T7 C7, T8 C8)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
    where T6 : Component, new()
    where T7 : Component, new()
    where T8 : Component, new()
{
    private QueryFilter _filter;

    internal QueryBuilder(string sceneName)
    {
        _filter = new QueryFilter(sceneName, Scene.BuildQueryString(
            Scene.GetNativeName<T1>(), Scene.GetNativeName<T2>(),
            Scene.GetNativeName<T3>(), Scene.GetNativeName<T4>(),
            Scene.GetNativeName<T5>(), Scene.GetNativeName<T6>(),
            Scene.GetNativeName<T7>(), Scene.GetNativeName<T8>()));
    }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> Without<TEx>() where TEx : Component, new()
    { _filter.AppendWithout(Scene.GetNativeNameOrEmpty<TEx>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> Without<TEx1, TEx2>() where TEx1 : Component, new() where TEx2 : Component, new()
    { Without<TEx1>(); return Without<TEx2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> Without<TEx1, TEx2, TEx3>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new()
    { Without<TEx1, TEx2>(); return Without<TEx3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> Without<TEx1, TEx2, TEx3, TEx4>() where TEx1 : Component, new() where TEx2 : Component, new() where TEx3 : Component, new() where TEx4 : Component, new()
    { Without<TEx1, TEx2, TEx3>(); return Without<TEx4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> With<TW>() where TW : Component, new()
    { _filter.AppendMustHave(Scene.GetNativeNameOrEmpty<TW>()); return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> With<TW1, TW2>() where TW1 : Component, new() where TW2 : Component, new()
    { With<TW1>(); return With<TW2>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> With<TW1, TW2, TW3>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new()
    { With<TW1, TW2>(); return With<TW3>(); }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> With<TW1, TW2, TW3, TW4>() where TW1 : Component, new() where TW2 : Component, new() where TW3 : Component, new() where TW4 : Component, new()
    { With<TW1, TW2, TW3>(); return With<TW4>(); }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> EnabledOnly() { _filter.EnableFilter = 1; return this; }
    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> DisabledOnly() { _filter.EnableFilter = 2; return this; }

    public QueryBuilder<T1, T2, T3, T4, T5, T6, T7, T8> WithCondition<TC>(Func<TC, bool> predicate) where TC : Component, new()
    {
        _filter.AddCondition(e => { var c = e.GetComponent<TC>(); return c != null && predicate(c); });
        return this;
    }

    public EntityQueryResult<T1, T2, T3, T4, T5, T6, T7, T8> WithEntity() => new EntityQueryResult<T1, T2, T3, T4, T5, T6, T7, T8>(_filter);

    public IEnumerator<(T1, T2, T3, T4, T5, T6, T7, T8)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6, out T7? c7, out T8? c8)) continue;
            yield return (c1!, c2!, c3!, c4!, c5!, c6!, c7!, c8!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

public struct EntityQueryResult<T1, T2, T3, T4, T5, T6, T7, T8> : IEnumerable<(Entity Entity, T1 C1, T2 C2, T3 C3, T4 C4, T5 C5, T6 C6, T7 C7, T8 C8)>
    where T1 : Component, new()
    where T2 : Component, new()
    where T3 : Component, new()
    where T4 : Component, new()
    where T5 : Component, new()
    where T6 : Component, new()
    where T7 : Component, new()
    where T8 : Component, new()
{
    private QueryFilter _filter;
    internal EntityQueryResult(QueryFilter filter) { _filter = filter; }

    public IEnumerator<(Entity, T1, T2, T3, T4, T5, T6, T7, T8)> GetEnumerator()
    {
        foreach (ulong id in _filter.Execute())
        {
            var entity = new Entity(id);
            if (!_filter.PassesConditions(entity)) continue;
            if (!Scene.TryGetComponents(entity, out T1? c1, out T2? c2, out T3? c3, out T4? c4, out T5? c5, out T6? c6, out T7? c7, out T8? c8)) continue;
            yield return (entity, c1!, c2!, c3!, c4!, c5!, c6!, c7!, c8!);
        }
    }
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
