package com.sdv.killcontrol.sdv.protocol.v1;

public enum ErrorCode {
    OK(0),

    INVALID_JSON(1000),
    MISSING_FIELD(1001),
    INVALID_FIELD(1002),
    UNSUPPORTED_VERSION(1003),
    UNKNOWN_COMMAND(1004),

    FEATURE_STOP_FAILED(2001),
    DEPLOY_FAILED(2002),

    PUBLISH_FAILED(3001);

    private final int code;

    ErrorCode(int code) {
        this.code = code;
    }

    public int code() {
        return code;
    }
}

