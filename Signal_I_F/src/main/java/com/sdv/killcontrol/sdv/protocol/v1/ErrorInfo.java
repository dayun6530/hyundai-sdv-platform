package com.sdv.killcontrol.sdv.protocol.v1;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public record ErrorInfo(
        int code,
        String message
) {
    public static ErrorInfo of(ErrorCode code, String message) {
        return new ErrorInfo(code.code(), message);
    }
}

