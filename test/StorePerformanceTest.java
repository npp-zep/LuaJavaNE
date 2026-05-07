package com.luajava;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class StorePerformanceTest extends BaseTest {

    @Test
    void storeVsJavaFieldInLua() {
        L.doString("java = require('java')");
        L.doString("Point = java.import('java.awt.Point'); p = Point:new(0,0)");
        L.doString(
            "function benchWrite()\n" +
            "  local iterations = 2000\n" +
            "  local start = os.clock()\n" +
            "  for i = 1, iterations do java.store('bench', i) end\n" +
            "  local storeTime = os.clock() - start\n" +
            "  start = os.clock()\n" +
            "  for i = 1, iterations do p.x = i end\n" +
            "  local fieldTime = os.clock() - start\n" +
            "  return storeTime, fieldTime\n" +
            "end\n"
        );
        Object[] results = L.callFunctionMultiple("benchWrite");
        double storeTime = ((Number) results[0]).doubleValue();
        double fieldTime = ((Number) results[1]).doubleValue();
        assertTrue(storeTime < fieldTime,
            "C store (" + storeTime + "s) should be faster than Java field (" + fieldTime + "s)");
    }

    @Test
    void storeVsGlobalReadInLua() {
        L.doString("java = require('java')");
        L.doString("java.store('val', 42); _G.globalVal = 42");
        L.doString(
            "function benchRead()\n" +
            "  local iterations = 5000\n" +
            "  local start = os.clock()\n" +
            "  for i = 1, iterations do _ = java.fetch('val') end\n" +
            "  local storeTime = os.clock() - start\n" +
            "  start = os.clock()\n" +
            "  for i = 1, iterations do _ = _G.globalVal end\n" +
            "  local globalTime = os.clock() - start\n" +
            "  return storeTime, globalTime\n" +
            "end\n"
        );
        Object[] results = L.callFunctionMultiple("benchRead");
        double storeTime = ((Number) results[0]).doubleValue();
        double globalTime = ((Number) results[1]).doubleValue();
        double ratio = storeTime / globalTime;
        assertTrue(ratio < 5.0,
            "C store read (" + storeTime + "s) should be within 5x of Lua global read (" + globalTime + "s), was " + ratio + "x");
    }
}
