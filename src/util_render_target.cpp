/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2019 terryky1220@gmail.com
 * ------------------------------------------------ */
#include "pch.h"
#include "util_egl.h"

#include "util_render_target.h"

int
create_render_target(render_target_t *rtarget, int w, int h) {
    /* texture for color */
    GLuint tex_c = 0;
    glGenTextures(1, &tex_c);
    glBindTexture(GL_TEXTURE_2D, tex_c);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);


    /* texture for depth */
    GLuint tex_z = 0;
//    glGenTextures(1, &tex_z);
//    glBindTexture(GL_TEXTURE_2D, tex_z);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, w, h, 0, GL_DEPTH_COMPONENT,
//                 GL_UNSIGNED_INT, nullptr);


    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_c, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex_z, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    memset(rtarget, 0, sizeof(*rtarget));
    rtarget->texc_id = tex_c;
    rtarget->texz_id = tex_z;
    rtarget->fbo_id = fbo;
    rtarget->width = w;
    rtarget->height = h;

    return 0;
}


int
destroy_render_target(render_target_t *rtarget) {
    glDeleteTextures(1, &rtarget->texc_id);
    glDeleteTextures(1, &rtarget->texz_id);
    glDeleteFramebuffers(1, &rtarget->fbo_id);
    memset(rtarget, 0, sizeof(*rtarget));

    return 0;
}

int
set_render_target(render_target_t *rtarget) {
    if (rtarget) {
        glBindFramebuffer(GL_FRAMEBUFFER, rtarget->fbo_id);
        glViewport(0, 0, rtarget->width, rtarget->height);
        glScissor(0, 0, rtarget->width, rtarget->height);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    return 0;
}

int
get_render_target(render_target_t *rtarget) {
    GLint tex_c, tex_z, fbo;
    int viewport[4];

    memset(rtarget, 0, sizeof(*rtarget));

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    if (fbo > 0) {
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
                                              GL_COLOR_ATTACHMENT0,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                              &tex_c);

        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
                                              GL_DEPTH_ATTACHMENT,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                              &tex_z);
        rtarget->fbo_id = fbo;
        rtarget->texc_id = tex_c;
        rtarget->texz_id = tex_z;
    }

    glGetIntegerv(GL_VIEWPORT, viewport);
    rtarget->width = viewport[2];
    rtarget->height = viewport[3];

    return 0;
}

