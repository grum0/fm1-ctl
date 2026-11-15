package com.grum0.gen1

import android.app.Application

class Gen1App : Application() {
    lateinit var store: Store

    override fun onCreate() {
        super.onCreate()
        Native.nInit()
        store = Store(this)
        store.load()
        store.hub.start()
    }
}
