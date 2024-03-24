package com.example.googletest

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView
import com.example.googletest.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Example of a call to a native method
        binding.sampleText.text = add(1, 2).toString()
    }

    /**
     * A native method that is implemented by the 'googletest' native library,
     * which is packaged with this application.
     */
    external fun add(a: Int, b: Int): Int

    companion object {
        // Used to load the 'googletest' library on application startup.
        init {
            System.loadLibrary("googletest-example")
        }
    }
}