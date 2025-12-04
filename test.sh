#!/bin/bash

# ============================================
# Test Script for Remote File System
# Tests: WRITE, GET, RM, STOP, Versioning, Multi-threading
# ============================================

echo "============================================"
echo "Remote File System - Test Suite"
echo "============================================"
echo ""

# Clean up before tests
echo "[SETUP] Cleaning up..."
rm -rf server_root
rm -f test.txt test2.txt bigfile.bin downloaded.txt latest.txt old1.txt fileA.txt fileB.txt
echo ""

# Start server in background
echo "[SETUP] Starting server..."
./server &
SERVER_PID=$!
sleep 1
echo "Server started with PID: $SERVER_PID"
echo ""

# ============================================
# Q1: WRITE Tests
# ============================================
echo "============================================"
echo "Q1: WRITE Command Tests"
echo "============================================"

# Test 1: Basic WRITE
echo "[TEST 1] Basic WRITE command"
echo "Hello World" > test.txt
./rfs WRITE test.txt folder/test.txt
if [ -f "server_root/folder/test.txt" ]; then
    echo "PASS: File written to server"
else
    echo "FAIL: File not found on server"
fi
echo ""

# Test 2: WRITE with default remote path
echo "[TEST 2] WRITE with default remote path"
echo "Default path test" > test2.txt
./rfs WRITE test2.txt
if [ -f "server_root/test2.txt" ]; then
    echo "PASS: File written with default path"
else
    echo "FAIL: File not found on server"
fi
echo ""

# Test 3: WRITE with custom host/port
echo "[TEST 3] WRITE with custom host/port"
echo "Custom host test" > test3.txt
./rfs -h 127.0.0.1 -p 2000 WRITE test3.txt folder/test3.txt
if [ -f "server_root/folder/test3.txt" ]; then
    echo "PASS: File written with custom host/port"
else
    echo "FAIL: File not found on server"
fi
rm -f test3.txt
echo ""

# ============================================
# Q2: GET Tests
# ============================================
echo "============================================"
echo "Q2: GET Command Tests"
echo "============================================"

# Test 4: Basic GET
echo "[TEST 4] Basic GET command"
./rfs GET folder/test.txt downloaded.txt
if [ -f "downloaded.txt" ]; then
    echo "PASS: File downloaded from server"
    echo "Content: $(cat downloaded.txt)"
else
    echo "FAIL: File not downloaded"
fi
rm -f downloaded.txt
echo ""

# Test 5: GET with default local path
echo "[TEST 5] GET with default local path"
./rfs GET folder/test.txt
if [ -f "test.txt" ]; then
    echo "PASS: File downloaded with default path"
else
    echo "FAIL: File not downloaded"
fi
echo ""

# ============================================
# Q5: VERSIONING Tests
# ============================================
echo "============================================"
echo "Q5: VERSIONING Tests"
echo "============================================"

# Test 6: Version creation
echo "[TEST 6] Version creation on WRITE"
echo "Version 1" > test.txt
./rfs WRITE test.txt folder/test.txt

echo "Version 2" > test.txt
./rfs WRITE test.txt folder/test.txt

echo "Version 3" > test.txt
./rfs WRITE test.txt folder/test.txt

if [ -f "server_root/folder/test.txt.v1" ] && [ -f "server_root/folder/test.txt.v2" ]; then
    echo "PASS: Versions created"
    echo "Files: $(ls server_root/folder/)"
else
    echo "FAIL: Versions not created"
fi
echo ""

# Test 7: GET returns latest version
echo "[TEST 7] GET returns latest version"
./rfs GET folder/test.txt latest.txt
CONTENT=$(cat latest.txt)
if [ "$CONTENT" = "Version 3" ]; then
    echo "PASS: GET returns latest version"
else
    echo "FAIL: GET did not return latest version (got: $CONTENT)"
fi
rm -f latest.txt
echo ""

# Test 8: Can GET older versions
echo "[TEST 8] GET older versions"
./rfs GET folder/test.txt.v1 old1.txt
CONTENT=$(cat old1.txt)
if [ "$CONTENT" = "Version 1" ]; then
    echo "PASS: Can retrieve older version"
else
    echo "FAIL: Could not retrieve older version (got: $CONTENT)"
fi
rm -f old1.txt
echo ""

# ============================================
# Q3: RM Tests
# ============================================
echo "============================================"
echo "Q3: RM Command Tests"
echo "============================================"

# Test 9: RM deletes all versions
echo "[TEST 9] RM deletes file and all versions"
./rfs RM folder/test.txt

if [ ! -f "server_root/folder/test.txt" ] && [ ! -f "server_root/folder/test.txt.v1" ] && [ ! -f "server_root/folder/test.txt.v2" ]; then
    echo "PASS: File and all versions deleted"
else
    echo "FAIL: Some files still exist"
    ls server_root/folder/
fi
echo ""

# Test 10: RM error on non-existent file
echo "[TEST 10] RM error on non-existent file"
OUTPUT=$(./rfs RM nonexistent.txt 2>&1)
if echo "$OUTPUT" | grep -q "error occured!"; then
    echo "PASS: Error returned for non-existent file"
else
    echo "FAIL: No error for non-existent file"
fi
echo ""

# ============================================
# Q4: MULTI-THREADING Tests
# ============================================
echo "============================================"
echo "Q4: MULTI-THREADING Tests"
echo "============================================"

# Test 11: Simultaneous connections
echo "[TEST 11] Simultaneous client connections"
echo "File A" > fileA.txt
echo "File B" > fileB.txt

./rfs WRITE fileA.txt folder/fileA.txt &
PID1=$!
./rfs WRITE fileB.txt folder/fileB.txt &
PID2=$!

wait $PID1
wait $PID2

if [ -f "server_root/folder/fileA.txt" ] && [ -f "server_root/folder/fileB.txt" ]; then
    echo "PASS: Both files written simultaneously"
else
    echo "FAIL: Simultaneous writes failed"
fi
rm -f fileA.txt fileB.txt
echo ""

# ============================================
# STOP Command Test
# ============================================
echo "============================================"
echo "STOP Command Test"
echo "============================================"

echo "[TEST 12] STOP command shuts down server"
./rfs STOP
sleep 1

if ps -p $SERVER_PID > /dev/null 2>&1; then
    echo "FAIL: Server still running"
    kill $SERVER_PID 2>/dev/null
else
    echo "PASS: Server stopped successfully"
fi
echo ""

# ============================================
# Cleanup
# ============================================
echo "============================================"
echo "Cleanup"
echo "============================================"
rm -f test.txt test2.txt downloaded.txt
echo "Test files cleaned up"
echo ""

echo "============================================"
echo "All tests completed!"
echo "============================================"