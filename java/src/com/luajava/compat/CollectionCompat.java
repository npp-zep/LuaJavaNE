package com.luajava.compat;

import java.util.*;

public class CollectionCompat {
    public static ArrayList<Object> arrayList() { return new ArrayList<>(); }
    public static void listAdd(ArrayList<Object> list, Object item) { list.add(item); }
    public static Object listGet(ArrayList<Object> list, int idx) { return list.get(idx); }
    public static int listSize(ArrayList<Object> list) { return list.size(); }

    public static HashMap<Object, Object> hashMap() { return new HashMap<>(); }
    public static void mapPut(HashMap<Object, Object> map, Object key, Object val) { map.put(key, val); }
    public static Object mapGet(HashMap<Object, Object> map, Object key) { return map.get(key); }
}
