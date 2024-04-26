# Unit testing with the NDK

This NDK example shows how to write unit tests for native code with
[googletest](https://github.com/google/googletest), and run them  on-device.

There are 3 parts to this tutorial:

 1. Creating a default "Hello, world" C++ app.
 2. Creating a library and calling it from the app.
 3. Writing and running unit tests.

## Part 1. App creation

### Install Android Studio and the NDK

* Install [Android Studio](https://developer.android.com/studio).
* Open the [SDK manager](https://developer.android.com/studio/intro/update#sdk-manager) in Android
  Studio, and [install the NDK](https://developer.android.com/studio/projects/install-ndk).

### Create a C++ project

Create a new "Native C++" project in Android Studio. Refer to the instructions
[here](https://developer.android.com/studio/projects/add-native-code#new-project).

### Run the app

The app should display "Hello, world".

## Part 2. Adding a library and calling it from the app.

### Write the library

Here is a very simple library with a single function to add two numbers:

app/src/main/cpp/adder.h:

```C++
#ifndef GOOGLETEST_ADDER_H
#define GOOGLETEST_ADDER_H

int add(int a, int b);

#endif //GOOGLETEST_ADDER_H
```

app/src/main/cpp/adder.cpp:

```C++
#include "adder.h"

int add(int a, int b) {
    return a + b;
}
```

### Call the library with JNI

Modify the JNI interface in app/src/main/cpp/native_lib.cpp. Change it from a function that
returns "Hello, world" to a function that uses the library to add two numbers:

```C++
#include <jni.h>

#include "adder.h"

extern "C" JNIEXPORT jint JNICALL
Java_com_example_googletest_MainActivity_add(
        JNIEnv* env,
        jobject /* this */,
        jint a,
        jint b) {
    return add((int)a, (int)b);
}
```

And modify the Kotlin code accordingly:

```diff
--- a/googletest/app/src/main/java/com/example/googletest/MainActivity.kt
+++ b/googletest/app/src/main/java/com/example/googletest/MainActivity.kt
@@ -16,19 +16,19 @@ class MainActivity : AppCompatActivity() {
         setContentView(binding.root)

         // Example of a call to a native method
-        binding.sampleText.text = stringFromJNI()
+        binding.sampleText.text = add(1, 2).toString()
     }

     /**
      * A native method that is implemented by the 'googletest' native library,
      * which is packaged with this application.
      */
-    external fun stringFromJNI(): String
+    external fun add(a: Int, b: Int): Int

     companion object {
         // Used to load the 'googletest' library on application startup.
         init {
-            System.loadLibrary("googletest")
+            System.loadLibrary("googletest-example")
         }
     }
 }
```

### Modify the build rules

In app/src/main/cpp/CMakeLists.txt, add the adder library:

`add_library(adder OBJECT adder.cpp)`

We add it as an `OBJECT` because we will link it to both the JNI library and, in part 3,
the unit tests, and it's more efficient to not go all the way to producing a .so.

Add to the list of source files for the JNI library, next to native-lib.cpp:

`$<TARGET_OBJECTS:adder>`

### Build and run your app

It should display "3", calculated in a convoluted way by calling our native adder library.

## Add unit tests and run them on-device

### Write unit tests

Create app/src/main/cpp/adder_test.cpp:

```C++
#include "adder.h"

#include "gtest/gtest.h"

TEST(adder, adder) {
    EXPECT_EQ(3, add(1,2));
}
```

### Add build rules

#### Gradle

Modify app/build.gradle to:
* Use prefab.
* Exclude the unit testing code from our APK.
* Depend on googletest and the junit-gtest wrapper.

```diff
--- a/googletest/app/build.gradle
+++ b/googletest/app/build.gradle
@@ -41,8 +41,15 @@ android {
         }
     }
     buildFeatures {
+        prefab true
         viewBinding true
     }
+    packagingOptions {
+        // Libraries that are wrongly included in the junit-gtest AAR that will
+        // end up in our APK if we don't explicitly exclude them.
+        exclude "**/libadder.so"
+        exclude "**/libapptest.so"
+        exclude "**/libc++_shared.so"
+    }
 }

 dependencies {
@@ -51,6 +58,8 @@ dependencies {
     implementation 'androidx.appcompat:appcompat:1.4.2'
     implementation 'com.google.android.material:material:1.6.1'
     implementation 'androidx.constraintlayout:constraintlayout:2.1.4'
+    implementation 'androidx.test.ext:junit-gtest:1.0.0-alpha01'
+    implementation 'com.android.ndk.thirdparty:googletest:1.11.0-beta-1'
     testImplementation 'junit:junit:4.13.2'
     androidTestImplementation 'androidx.test.ext:junit:1.1.3'
     androidTestImplementation 'androidx.test.espresso:espresso-core:3.4.0'
```

#### CMake

Modify app/src/main/cpp/CMakeLists.txt.

Add `find_package` for googletest and junit-gtest:

```cmake
find_package(googletest REQUIRED CONFIG)
find_package(junit-gtest REQUIRED CONFIG)
```

Add a library for the unit tests:

```cmake
add_library(app_tests SHARED adder_test.cpp)
target_link_libraries(app_tests
  PRIVATE
    $<TARGET_OBJECTS:adder>
    googletest::gtest
    junit-gtest::junit-gtest
)
```

### Add a wrapper to run the unit tests

Create app/src/androidTest/java/com/example/googletest/NativeTests.kt as follows:

```kotlin
package com.example.googletest

import androidx.test.ext.junitgtest.GtestRunner
import androidx.test.ext.junitgtest.TargetLibrary
import org.junit.runner.RunWith

@RunWith(GtestRunner::class)
@TargetLibrary(libraryName = "app_tests")
class NativeTests
```

### Run the tests

In the project outline, right-click on the NativeTests you just created, and choose
"Run 'NativeTests'"

![Run 'NativeTests'](runtests.png)

Try deliberately breaking the unit test, like this:

```C++
EXPECT_EQ(4, add(1,2));
```

Now, when you run the tests, you should a failure message like this:

```
06-16 13:37:08.305  6055  6074 I TestRunner: started: adder_adder(com.example.googletest.NativeTests)
06-16 13:37:08.318  6055  6074 E TestRunner: failed: adder_adder(com.example.googletest.NativeTests)
06-16 13:37:08.318  6055  6074 E TestRunner: ----- begin exception -----
06-16 13:37:08.318  6055  6074 E TestRunner: java.lang.AssertionError:
06-16 13:37:08.318  6055  6074 E TestRunner: /Users/jamesfarrell/src/ndk-examples/googletest/app/src/main/cpp/adder_test.cpp:6
06-16 13:37:08.318  6055  6074 E TestRunner: Expected equality of these values:
06-16 13:37:08.318  6055  6074 E TestRunner:   4
06-16 13:37:08.318  6055  6074 E TestRunner:   add(1,2)
06-16 13:37:08.318  6055  6074 E TestRunner:     Which is: 3
06-16 13:37:08.318  6055  6074 E TestRunner:
06-16 13:37:08.318  6055  6074 E TestRunner: 	at androidx.test.ext.junitgtest.GtestRunner.run(Native Method)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at androidx.test.ext.junitgtest.GtestRunner.run(GtestRunner.kt:60)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.Suite.runChild(Suite.java:128)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.Suite.runChild(Suite.java:27)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.ParentRunner$4.run(ParentRunner.java:331)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.ParentRunner$1.schedule(ParentRunner.java:79)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.ParentRunner.runChildren(ParentRunner.java:329)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.ParentRunner.access$100(ParentRunner.java:66)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.ParentRunner$2.evaluate(ParentRunner.java:293)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.ParentRunner$3.evaluate(ParentRunner.java:306)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runners.ParentRunner.run(ParentRunner.java:413)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runner.JUnitCore.run(JUnitCore.java:137)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at org.junit.runner.JUnitCore.run(JUnitCore.java:115)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at androidx.test.internal.runner.TestExecutor.execute(TestExecutor.java:56)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at androidx.test.runner.AndroidJUnitRunner.onStart(AndroidJUnitRunner.java:444)
06-16 13:37:08.318  6055  6074 E TestRunner: 	at android.app.Instrumentation$InstrumentationThread.run(Instrumentation.java:2278)
06-16 13:37:08.318  6055  6074 E TestRunner: ----- end exception -----
06-16 13:37:08.319  6055  6074 I TestRunner: finished: adder_adder(com.example.googletest.NativeTests)

java.lang.AssertionError:
/Users/jamesfarrell/src/ndk-examples/googletest/app/src/main/cpp/adder_test.cpp:6
Expected equality of these values:
4
add(1,2)
Which is: 3

at androidx.test.ext.junitgtest.GtestRunner.run(Native Method)
at androidx.test.ext.junitgtest.GtestRunner.run(GtestRunner.kt:60)
at org.junit.runners.Suite.runChild(Suite.java:128)
at org.junit.runners.Suite.runChild(Suite.java:27)
at org.junit.runners.ParentRunner$4.run(ParentRunner.java:331)
at org.junit.runners.ParentRunner$1.schedule(ParentRunner.java:79)
at org.junit.runners.ParentRunner.runChildren(ParentRunner.java:329)
at org.junit.runners.ParentRunner.access$100(ParentRunner.java:66)
at org.junit.runners.ParentRunner$2.evaluate(ParentRunner.java:293)
at org.junit.runners.ParentRunner$3.evaluate(ParentRunner.java:306)
at org.junit.runners.ParentRunner.run(ParentRunner.java:413)
at org.junit.runner.JUnitCore.run(JUnitCore.java:137)
at org.junit.runner.JUnitCore.run(JUnitCore.java:115)
at androidx.test.internal.runner.TestExecutor.execute(TestExecutor.java:56)
at androidx.test.runner.AndroidJUnitRunner.onStart(AndroidJUnitRunner.java:444)
at android.app.Instrumentation$InstrumentationThread.run(Instrumentation.java:2278)
```