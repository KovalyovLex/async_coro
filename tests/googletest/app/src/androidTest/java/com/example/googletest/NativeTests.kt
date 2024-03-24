package com.example.googletest

import androidx.test.ext.junitgtest.GtestRunner
import androidx.test.ext.junitgtest.TargetLibrary
import org.junit.runner.RunWith

@RunWith(GtestRunner::class)
@TargetLibrary(libraryName = "async_coro_tests")
class NativeTests
