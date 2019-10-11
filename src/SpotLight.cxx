#include "easy_pbr/SpotLight.h"

//loguru
#define LOGURU_REPLACE_GLOG 1
#include <loguru.hpp>

#include "easy_pbr/MeshGL.h"


SpotLight::SpotLight(const configuru::Config& config)
    // m_fov_x(90),
    // m_fov_y(90),
    // m_fov(90),
    // m_near(0.01)
    // m_far(5000)
{
    init_params(config);

    // m_fov=90;
    m_near=0.1; //a light will not be as close to the object as the default cam so we can set this to quite big
    // m_far=5000;
    // m_shadow_map_resolution=2048;
    // m_color << 1.0, 1.0, 1.0;
    // m_power=1.0;
    // m_create_shadow=true;

    init_opengl();
}

void SpotLight::init_params(const configuru::Config& config ){

    m_power=config["power"];
    m_color=config["color"];
    m_create_shadow=config["create_shadow"];
    m_shadow_map_resolution=config["shadow_map_resolution"];

}

void SpotLight::init_opengl(){
    m_shadow_map_shader.compile( std::string(CMAKE_SOURCE_DIR)+"/shaders/render/shadow_map_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/shadow_map_frag.glsl"  );
}

// void PointLight::render_to_shadow_map(const MeshCore& mesh){
void SpotLight::render_mesh_to_shadow_map(MeshGLSharedPtr& mesh){

    //add a depth texture to the framebuffer of the shadow map
    if(!m_shadow_map_fbo.is_initialized() || m_shadow_map_fbo.width()!=m_shadow_map_resolution || m_shadow_map_fbo.height()!=m_shadow_map_resolution ){
        m_shadow_map_fbo.set_size(m_shadow_map_resolution, m_shadow_map_resolution);
        m_shadow_map_fbo.add_depth("shadow_map_depth");
        //make the depth map have nearest sampling because that will work best for shadow mapping
        // m_shadow_map_fbo.tex_with_name("shadow_map_depth").set_filter_mode(GL_NEAREST);

        m_shadow_map_fbo.sanity_check();
    } 


    // Set attributes that the vao will pulll from buffers
    GL_C( mesh->vao.vertex_attribute(m_shadow_map_shader, "position", mesh->V_buf, 3) );
    GL_C( mesh->vao.indices(mesh->F_buf) ); //Says the indices with we refer to vertices, this gives us the triangles

    //matrices setup
    Eigen::Vector2f viewport_size;
    viewport_size<< m_shadow_map_resolution, m_shadow_map_resolution;
    Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    Eigen::Matrix4f V = view_matrix();
    Eigen::Matrix4f P = proj_matrix(viewport_size);
    Eigen::Matrix4f MVP = P*V*M;
 

    GL_C( glViewport(0,0,m_shadow_map_resolution,m_shadow_map_resolution) );
    GL_C( m_shadow_map_shader.use() );
    m_shadow_map_shader.uniform_4x4(MVP, "MVP");
    m_shadow_map_shader.draw_into(m_shadow_map_fbo, {} ); //makes the shaders draw into the buffers we defines in the gbuffer

    // draw
    GL_C( mesh->vao.bind() ); 
    GL_C( glDrawElements(GL_TRIANGLES, mesh->m_core->F.size(), GL_UNSIGNED_INT, 0) );

    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
    GL_C( glBindFramebuffer(GL_FRAMEBUFFER, 0) );

}

void SpotLight::render_points_to_shadow_map(MeshGLSharedPtr& mesh){

    //add a depth texture to the framebuffer of the shadow map
    if(!m_shadow_map_fbo.is_initialized() || m_shadow_map_fbo.width()!=m_shadow_map_resolution || m_shadow_map_fbo.height()!=m_shadow_map_resolution ){
        m_shadow_map_fbo.set_size(m_shadow_map_resolution, m_shadow_map_resolution);
        m_shadow_map_fbo.add_depth("shadow_map_depth");
        m_shadow_map_fbo.sanity_check();
    } 


    // // Set attributes that the vao will pulll from buffers
    // GL_C( mesh->vao.vertex_attribute(m_shadow_map_shader, "position", mesh->V_buf, 3) );
    // GL_C( mesh->vao.indices(mesh->F_buf) ); //Says the indices with we refer to vertices, this gives us the triangles

    // //matrices setup
    // Eigen::Vector2f viewport_size;
    // viewport_size<< m_shadow_map_resolution, m_shadow_map_resolution;
    // Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    // Eigen::Matrix4f V = view_matrix();
    // Eigen::Matrix4f P = proj_matrix(viewport_size);
    // Eigen::Matrix4f MVP = P*V*M;
 

    // GL_C( glViewport(0,0,m_shadow_map_resolution,m_shadow_map_resolution) );
    // GL_C( m_shadow_map_shader.use() );
    // m_shadow_map_shader.uniform_4x4(MVP, "MVP");
    // m_shadow_map_shader.draw_into(m_shadow_map_fbo, {} ); //makes the shaders draw into the buffers we defines in the gbuffer

    // // draw
    // GL_C( mesh->vao.bind() ); 
    // GL_C( glDrawElements(GL_TRIANGLES, mesh->m_core->F.size(), GL_UNSIGNED_INT, 0) );

    // GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
    // GL_C( glBindFramebuffer(GL_FRAMEBUFFER, 0) );

}

void SpotLight::clear_shadow_map(){
    if (has_shadow_map()){
        m_shadow_map_fbo.clear();
    }
}

void SpotLight::set_shadow_map_resolution(const int shadow_map_resolution){
    m_shadow_map_resolution=shadow_map_resolution;
}

int SpotLight::shadow_map_resolution(){
    return m_shadow_map_resolution;
}

bool SpotLight::has_shadow_map(){
    return m_shadow_map_fbo.has_tex_with_name("shadow_map_depth");
}

gl::Texture2D& SpotLight::get_shadow_map_ref(){
    return m_shadow_map_fbo.tex_with_name("shadow_map_depth");
}