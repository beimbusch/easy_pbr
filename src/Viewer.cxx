#include "easy_pbr/Viewer.h"

//opengl stuff 
#include <glad/glad.h> // Initialize with gladLoadGL()
// Include glfw3.h after our OpenGL definitions
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h" 

#include <string> //find_last_of
#include <limits> //signaling_nan

//loguru
#define LOGURU_IMPLEMENTATION 1
#define LOGURU_NO_DATE_TIME 1
#define LOGURU_NO_UPTIME 1
#define LOGURU_REPLACE_GLOG 1
#include <loguru.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h> //glfw3.h after our OpenGL definitions
 
//My stuff 
#include "UtilsGL.h"
#include "easy_pbr/Scene.h"
#include "easy_pbr/Camera.h"
#include "easy_pbr/MeshGL.h"
#include "easy_pbr/Mesh.h"
#include "easy_pbr/Gui.h"
#include "easy_pbr/SpotLight.h"
#include "easy_pbr/Recorder.h"
#include "easy_pbr/LabelMngr.h"
#include "RandGenerator.h"
#include "opencv_utils.h"
#include "string_utils.h"

//Add this header after we add all opengl stuff because we need the profiler to have glFinished defined
#define PROFILER_IMPLEMENTATION 1
#define ENABLE_GL_PROFILING 1
#include "Profiler.h" 


//configuru
#define CONFIGURU_IMPLEMENTATION 1
#define CONFIGURU_WITH_EIGEN 1
#define CONFIGURU_IMPLICIT_CONVERSIONS 1
#include <configuru.hpp>
using namespace configuru;

//boost
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

using namespace easy_pbr::utils;

//ros
// #include "easy_pbr/utils/RosTools.h"

Viewer::Viewer(const std::string config_file):
   dummy( init_context() ),
   dummy_glad(gladLoadGL() ),
    #ifdef WITH_DIR_WATCHER 
        dir_watcher(std::string(PROJECT_SOURCE_DIR)+"/shaders/",5),
    #endif
    m_scene(new Scene),
    // m_gui(new Gui(this, m_window )),
    m_default_camera(new Camera),
    m_recorder(new Recorder()),
    m_rand_gen(new RandGenerator()),
    m_viewport_size(640, 480),
    m_background_color(0.2, 0.2, 0.2),
    m_draw_points_shader("draw_points"),
    m_draw_lines_shader("draw_lines"),
    m_draw_mesh_shader("draw_mesh"),
    m_draw_wireframe_shader("draw_wireframe"),
    m_rvec_tex("rvec_tex"),
    m_fullscreen_quad(MeshGL::create()),
    m_ssao_downsample(1),
    m_nr_samples(64),
    m_kernel_radius(-1),
    m_ao_power(9),
    m_sigma_spacial(2.0),
    m_sigma_depth(0.002),
    m_ambient_color( 71.0/255.0, 70.0/255.0, 66.3/255.0  ),
    m_ambient_color_power(0.05),
    m_enable_culling(false),
    m_enable_ssao(true),
    m_enable_bloom(true),
    m_bloom_threshold(0.85),
    m_bloom_mip_map_lvl(1),
    m_bloom_blur_iters(3),
    m_lights_follow_camera(false),
    m_environment_cubemap_resolution(512),
    m_irradiance_cubemap_resolution(32),
    m_prefilter_cubemap_resolution(128),
    m_brdf_lut_resolution(512),
    m_first_draw(true)
    {
        m_camera=m_default_camera;
        init_params(config_file);
        compile_shaders(); 
        init_opengl();                     
        m_gui=std::make_shared<Gui>(config_file, this, m_window); //needs to be initialized here because here we have done a gladloadgl

}

Viewer::~Viewer(){
    // LOG(WARNING) << "Destroying viewer";
}

void Viewer::init_params(const std::string config_file){

    //read all the parameters
    // Config cfg = configuru::parse_file(std::string(CMAKE_SOURCE_DIR)+"/config/"+config_file, CFG);
    std::string config_file_abs;
    if (fs::path(config_file).is_relative()){
        config_file_abs=(fs::path(PROJECT_SOURCE_DIR) / config_file).string();
    }else{
        config_file_abs=config_file;
    }

    Config cfg = configuru::parse_file(config_file_abs, CFG);
    Config vis_config=cfg["visualization"];
    //general
    m_show_gui = vis_config["show_gui"];
    m_subsample_factor = vis_config["subsample_factor"];
    m_enable_culling = vis_config["enable_culling"];
    //cam
    m_camera->m_fov=vis_config["cam"].get_float_else_nan("fov")  ;
    m_camera->m_near=vis_config["cam"].get_float_else_nan("near")  ;
    m_camera->m_far=vis_config["cam"].get_float_else_nan("far");
    m_camera->m_exposure=vis_config["cam"].get_float_else_nan("exposure");


    //ssao
    m_auto_ssao= vis_config["ssao"]["auto_settings"];
    m_enable_ssao = vis_config["ssao"]["enable_ssao"];
    m_ssao_downsample = vis_config["ssao"]["ao_downsample"];
    m_kernel_radius = vis_config["ssao"].get_float_else_nan("kernel_radius");
    m_ao_power = vis_config["ssao"]["ao_power"];
    m_sigma_spacial = vis_config["ssao"]["ao_blur_sigma_spacial"];
    m_sigma_depth = vis_config["ssao"]["ao_blur_sigma_depth"];

    //bloom
    m_enable_bloom = vis_config["bloom"]["enable_bloom"];
    m_bloom_threshold = vis_config["bloom"]["threshold"];
    m_bloom_mip_map_lvl = vis_config["bloom"]["mip_map_lvl"];
    m_bloom_blur_iters = vis_config["bloom"]["blur_iters"];

    //edl
    m_auto_edl= vis_config["edl"]["auto_settings"];
    m_enable_edl_lighting= vis_config["edl"]["enable_edl_lighting"];
    m_edl_strength = vis_config["edl"]["edl_strength"];

    //background
    m_show_background_img = vis_config["background"]["show_background_img"];
    m_background_img_path = (std::string)vis_config["background"]["background_img_path"];

    //ibl
    m_enable_ibl = vis_config["ibl"]["enable_ibl"];
    m_show_environment_map = vis_config["ibl"]["show_environment_map"];
    m_environment_map_path = (fs::path(EASYPBR_DATA_DIR) / (std::string)vis_config["ibl"]["environment_map_path"]).string();
    m_environment_cubemap_resolution = vis_config["ibl"]["environment_cubemap_resolution"];
    m_irradiance_cubemap_resolution = vis_config["ibl"]["irradiance_cubemap_resolution"];
    m_prefilter_cubemap_resolution = vis_config["ibl"]["prefilter_cubemap_resolution"];
    m_brdf_lut_resolution = vis_config["ibl"]["brdf_lut_resolution"];

    //create the spot lights
    int nr_spot_lights=vis_config["lights"]["nr_spot_lights"];
    for(int i=0; i<nr_spot_lights; i++){   
        Config light_cfg=vis_config["lights"]["spot_light_"+std::to_string(i)];
        std::shared_ptr<SpotLight> light= std::make_shared<SpotLight>(light_cfg);
        m_spot_lights.push_back(light);
    }

}

bool Viewer::init_context(){
    // GLFWwindow* window;
    int window_width, window_height;
    window_width=640;
    window_height=480;

     // Setup window
    if (!glfwInit()){
        LOG(FATAL) << "GLFW could not initialize";
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    m_window = glfwCreateWindow(window_width, window_height, "Renderer",nullptr,nullptr);
    if (!m_window){
        LOG(FATAL) << "GLFW window creation failed. It may be that you are requesting a too high version of opengl that is not supported by your drivers. It may happen especially if you are running mesa drivers instead of nvidia.";        
        glfwTerminate();
    }
    glfwMakeContextCurrent(m_window);
    // Load OpenGL and its extensions
    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)){
        LOG(FATAL) << "GLAD failed to load";        
    }
    glfwSwapInterval(1); // Enable vsync

    glfwSetInputMode(m_window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);

    //window will be resized to the screen so we can return the actual widnow values now 
    glfwGetWindowSize(m_window, &window_width, &window_height);
    m_viewport_size<< window_width, window_height;


    glfwSetWindowUserPointer(m_window, this); // so in the glfw we can acces the viewer https://stackoverflow.com/a/28660673
    setup_callbacks_viewer(m_window);

    return true;
}

 void Viewer::setup_callbacks_viewer(GLFWwindow* window){
        // https://stackoverflow.com/a/28660673
        auto mouse_pressed_func = [](GLFWwindow* w, int button, int action, int modifier) {   static_cast<Viewer*>(glfwGetWindowUserPointer(w))->glfw_mouse_pressed( w, button, action, modifier );     };
        auto mouse_move_func = [](GLFWwindow* w, double x, double y) {   static_cast<Viewer*>(glfwGetWindowUserPointer(w))->glfw_mouse_move( w, x, y );     };
        auto mouse_scroll_func = [](GLFWwindow* w, double x, double y) {   static_cast<Viewer*>(glfwGetWindowUserPointer(w))->glfw_mouse_scroll( w, x, y );     }; 
        auto key_func = [](GLFWwindow* w, int key, int scancode, int action, int modifier) {   static_cast<Viewer*>(glfwGetWindowUserPointer(w))->glfw_key( w, key, scancode, action, modifier );     }; 
        auto char_mods_func  = [](GLFWwindow* w, unsigned int codepoint, int modifier) {   static_cast<Viewer*>(glfwGetWindowUserPointer(w))->glfw_char_mods( w, codepoint, modifier );     }; 
        auto resize_func = [](GLFWwindow* w, int width, int height) {   static_cast<Viewer*>(glfwGetWindowUserPointer(w))->glfw_resize( w, width, height );     };   
        auto drop_func = [](GLFWwindow* w, int count, const char** paths) {   static_cast<Viewer*>(glfwGetWindowUserPointer(w))->glfw_drop( w, count, paths );     };   
        

        glfwSetMouseButtonCallback(window, mouse_pressed_func);
        glfwSetCursorPosCallback(window, mouse_move_func);
        glfwSetScrollCallback(window,mouse_scroll_func);
        glfwSetKeyCallback(window, key_func);
        glfwSetCharModsCallback(window,char_mods_func);
        glfwSetWindowSizeCallback(window,resize_func);
        glfwSetDropCallback(window, drop_func);

}

void Viewer::setup_callbacks_imgui(GLFWwindow* window){
    glfwSetMouseButtonCallback(window, ImGui_ImplGlfw_MouseButtonCallback);
    glfwSetCursorPosCallback(window, nullptr);
    glfwSetScrollCallback(window, ImGui_ImplGlfw_ScrollCallback);
    glfwSetKeyCallback(window, ImGui_ImplGlfw_KeyCallback);
    glfwSetCharCallback(window, ImGui_ImplGlfw_CharCallback);
    glfwSetCharModsCallback(window, nullptr);
}


void Viewer::switch_callbacks(GLFWwindow* window) {
    // bool hovered_imgui = ImGui::IsMouseHoveringAnyWindow();
    bool using_imgui = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
    // VLOG(1) << "using imgui is " << using_imgui;

    // glfwSetMouseButtonCallback(window, nullptr);
    // glfwSetCursorPosCallback(window, nullptr);
    // glfwSetScrollCallback(window, nullptr);
    // glfwSetKeyCallback(window, nullptr);
    // glfwSetCharModsCallback(window, nullptr);
    // // glfwSetWindowSizeCallback(window, nullptr); //this should just be left to the one resize_func in this viewer
    // glfwSetCharCallback(window, nullptr);

    if (using_imgui) {
        setup_callbacks_imgui(window);
    } else {
        setup_callbacks_viewer(window);   
    }
}

void Viewer::compile_shaders(){
       
    m_draw_points_shader.compile( std::string(EASYPBR_SHADERS_PATH)+"/render/points_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/points_frag.glsl" ) ;
    m_draw_lines_shader.compile( std::string(EASYPBR_SHADERS_PATH)+"/render/lines_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/lines_frag.glsl"  );
    m_draw_mesh_shader.compile( std::string(EASYPBR_SHADERS_PATH)+"/render/mesh_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/mesh_frag.glsl"  );
    m_draw_wireframe_shader.compile( std::string(EASYPBR_SHADERS_PATH)+"/render/wireframe_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/wireframe_frag.glsl"  );
    m_draw_surfels_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/render/surfels_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/surfels_frag.glsl" , std::string(EASYPBR_SHADERS_PATH)+"/render/surfels_geom.glsl" );
    m_compose_final_quad_shader.compile( std::string(EASYPBR_SHADERS_PATH)+"/render/compose_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/compose_frag.glsl"  );
    m_blur_shader.compile( std::string(EASYPBR_SHADERS_PATH)+"/render/blur_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/blur_frag.glsl"  );
    m_apply_postprocess_shader.compile( std::string(EASYPBR_SHADERS_PATH)+"/render/apply_postprocess_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/render/apply_postprocess_frag.glsl"  );

    m_ssao_ao_pass_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/ssao/ao_pass_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/ssao/ao_pass_frag.glsl" );
    // m_depth_linearize_shader.compile(std::string(PROJECT_SOURCE_DIR)+"/shaders/ssao/depth_linearize_compute.glsl");
    m_depth_linearize_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/ssao/depth_linearize_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/ssao/depth_linearize_frag.glsl");
    m_bilateral_blur_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/ssao/bilateral_blur_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/ssao/bilateral_blur_frag.glsl");

    m_equirectangular2cubemap_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/ibl/equirectangular2cubemap_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/ibl/equirectangular2cubemap_frag.glsl");
    m_radiance2irradiance_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/ibl/radiance2irradiance_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/ibl/radiance2irradiance_frag.glsl");
    m_prefilter_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/ibl/prefilter_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/ibl/prefilter_frag.glsl");
    m_integrate_brdf_shader.compile(std::string(EASYPBR_SHADERS_PATH)+"/ibl/integrate_brdf_vert.glsl", std::string(EASYPBR_SHADERS_PATH)+"/ibl/integrate_brdf_frag.glsl");
}

void Viewer::init_opengl(){
    // //initialize the g buffer with some textures 
    GL_C( m_gbuffer.set_size(m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor ) ); //established what will be the size of the textures attached to this framebuffer
    GL_C( m_gbuffer.add_texture("diffuse_gtex", GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE) ); 
    GL_C( m_gbuffer.add_texture("normal_gtex", GL_RG16F, GL_RG, GL_HALF_FLOAT) );  //as done by Cry Engine 3 in their presentation "A bit more deferred"  https://www.slideshare.net/guest11b095/a-bit-more-deferred-cry-engine3
    GL_C( m_gbuffer.add_texture("metalness_and_roughness_gtex", GL_RG8, GL_RG, GL_UNSIGNED_BYTE) ); 
    GL_C( m_gbuffer.add_texture("mesh_id_gtex", GL_R8I, GL_RED_INTEGER, GL_INT) ); 
    GL_C( m_gbuffer.add_depth("depth_gtex") );
    m_gbuffer.sanity_check();

    //initialize the final fbo
    GL_C( m_final_fbo_no_gui.set_size(m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor ) ); //established what will be the size of the textures attached to this framebuffer
    GL_C( m_final_fbo_no_gui.add_texture("color_gtex", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE) ); 
    GL_C( m_final_fbo_no_gui.add_depth("depth_gtex") );
    m_final_fbo_no_gui.sanity_check();
    //initilize the final_fbo which also has the gui
    GL_C( m_final_fbo_with_gui.set_size(m_viewport_size.x(), m_viewport_size.y() ) ); //established what will be the size of the textures attached to this framebuffer
    GL_C( m_final_fbo_with_gui.add_texture("color_gtex", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE) ); 
    GL_C( m_final_fbo_with_gui.add_depth("depth_gtex") );
    m_final_fbo_with_gui.sanity_check();


    m_composed_fbo.set_size(m_gbuffer.width(), m_gbuffer.height() ); //established what will be the size of the textures attached to this framebuffer
    m_composed_fbo.add_texture("composed_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT); 
    m_composed_fbo.add_texture("bloom_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);  
    m_composed_fbo.sanity_check();


    // //DEBUG=============
    // GL_C( m_composed_fbo.tex_with_name("composed_gtex").set_val(m_background_color.x(), m_background_color.y(), m_background_color.z(), 0.0) );
    // // GL_C( m_composed_fbo.tex_with_name("bloom_gtex").set_val(m_background_color.x(), m_background_color.y(), m_background_color.z(), 0.0) );
    // GL_C( m_composed_fbo.sanity_check());
    // VLOG(1) << "INITIAT trying to clear the bloom_gtex";
    // GL_C( m_composed_fbo.tex_with_name("bloom_gtex").clear() );
    // VLOG(1) << "INITIAL finished clearing bloom gtex";




    //set all the normal buffer to nearest because we assume that the norm of it values can be used to recover the n.z. However doing a nearest neighbour can change the norm and therefore fuck everything up
    m_gbuffer.tex_with_name("normal_gtex").set_filter_mode_min_mag(GL_NEAREST);



    //cubemaps 
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); //to linearly filter across faces of the cube
    m_environment_cubemap_tex.allocate_tex_storage(GL_RGB16F, GL_RGB, GL_HALF_FLOAT, m_environment_cubemap_resolution, m_environment_cubemap_resolution);
    m_irradiance_cubemap_tex.allocate_tex_storage(GL_RGB16F, GL_RGB, GL_HALF_FLOAT, m_irradiance_cubemap_resolution, m_irradiance_cubemap_resolution);
    m_prefilter_cubemap_tex.allocate_tex_storage(GL_RGB16F, GL_RGB, GL_HALF_FLOAT, m_prefilter_cubemap_resolution, m_prefilter_cubemap_resolution);
    m_prefilter_cubemap_tex.set_filter_mode_min(GL_LINEAR_MIPMAP_LINEAR);
    m_prefilter_cubemap_tex.set_filter_mode_mag(GL_LINEAR);
    m_prefilter_cubemap_tex.generate_mipmap_full();

    //brdf_lut_tex 
    m_brdf_lut_tex.allocate_storage(GL_RG16F, GL_RG, GL_HALF_FLOAT, m_brdf_lut_resolution, m_brdf_lut_resolution);

    m_rvec_tex.set_wrap_mode(GL_REPEAT); //so we repeat the random vectors specified in this 4x4 matrix over the whole image

    // make a 4x4 texture for the random vectors that will be used for rotating the hemisphere 
    int rvec_tex_size=4;
    cv::Mat rvec_mat=cv::Mat(rvec_tex_size, rvec_tex_size, CV_32FC4);
    for(int x=0; x<rvec_tex_size; x++){
        for(int y=0; y<rvec_tex_size; y++){
            Eigen::Vector3f rvec;
            rvec.x()=m_rand_gen->rand_float(-1.0f, 1.0f);
            rvec.y()=m_rand_gen->rand_float(-1.0f, 1.0f);
            rvec.z()=0.0;
            // rvec.normalize();
            rvec_mat.at<cv::Vec4f>(y, x)[0]=rvec.x();
            rvec_mat.at<cv::Vec4f>(y, x)[1]=rvec.y();
            rvec_mat.at<cv::Vec4f>(y, x)[2]=0.0; //set to 0 because we rotate along the z axis of the hemisphere
            rvec_mat.at<cv::Vec4f>(y, x)[3]=1.0; //not actually used but we only use textures of channels 1,2 and 4 
        }
    }
    GL_C( m_rvec_tex.upload_from_cv_mat(rvec_mat,false) );
    //make some random samples in a hemisphere 
    create_random_samples_hemisphere();



    //create a fullscreen quad which we will use for composing the final image after the deffrred render pass
    m_fullscreen_quad->m_core->create_full_screen_quad();
    GL_C( m_fullscreen_quad->upload_to_gpu() );

    //add the background image 
    if(m_show_background_img){
        read_background_img(m_background_tex, m_background_img_path);
    }



    //initialize a cubemap 
    integrate_brdf(m_brdf_lut_tex); //we leave it outside the if because when we drag some hdr map into the viewer we don't want to integrate the brdf every time
    if(m_enable_ibl){
        read_background_img(m_background_tex, m_environment_map_path);
        //if it's equirectangular we convert it to cubemap because it is faster to sample
        equirectangular2cubemap(m_environment_cubemap_tex, m_background_tex);
        radiance2irradiance(m_irradiance_cubemap_tex, m_environment_cubemap_tex);
        prefilter(m_prefilter_cubemap_tex, m_environment_cubemap_tex);
    }


}

void Viewer::hotload_shaders(){
    #ifdef WITH_DIR_WATCHER

        std::vector<std::string> changed_files=dir_watcher.poll_files();
        if(changed_files.size()>0){
            compile_shaders();
        }

    #endif
}

void Viewer::configure_auto_params(){
    Eigen::Vector3f centroid = m_scene->get_centroid();
    float scale = m_scene->get_scale();

    // std::cout << " scene centroid " << centroid << std::endl;
    // std::cout << " scene scale " << scale << std::endl;

    //CAMERA------------
    if (!m_camera->m_is_initialized){
        m_camera->set_lookat(centroid);
        m_camera->set_position(centroid+Eigen::Vector3f::UnitZ()*5*scale+Eigen::Vector3f::UnitY()*0.5*scale); //move the eye backwards so that is sees the whole scene
        if (std::isnan(m_camera->m_fov) ){ //signaling nan indicates we should automatically set the values
            m_camera->m_fov=30 ;
        }
        if (std::isnan(m_camera->m_near) ){ //signaling nan indicates we should automatically set the values
            m_camera->m_near=( (centroid-m_camera->position()).norm()*0.01 ) ;
        }
        if (std::isnan(m_camera->m_far) ){
            m_camera->m_far=( (centroid-m_camera->position()).norm()*10 ) ;
        }
        if (std::isnan(m_camera->m_exposure) ){ //signaling nan indicates we should automatically set the values
            m_camera->m_exposure=1.0;
         } 
        
        m_camera->m_is_initialized=true;
    }

    //SSAO---------------
    if(m_auto_ssao){
        //enable the ssao only if all the meshes have normals
        m_enable_ssao=false;
        for(int i=0; i<m_scene->nr_meshes(); i++){
            if (m_scene->get_mesh_with_idx(i)->NV.size() ){
                m_enable_ssao=true;
                break;
            }
        }

        //set the settings
        if (std::isnan(m_kernel_radius) ){
            m_kernel_radius=0.05*scale;
        }
    }

    //EDL--------
    if (m_auto_edl ){
        //we enable edl only if all the meshes in the scene don't show any meshes
        m_enable_edl_lighting=true;
        for(int i=0; i<m_scene->nr_meshes(); i++){
            if (m_scene->get_mesh_with_idx(i)->m_vis.m_show_mesh){
                m_enable_edl_lighting=false;
                break;
            }
        }
    }
    

    //LIGHTS-----------------------
    //key light
    if(m_spot_lights.size()>=1){
        std::shared_ptr<SpotLight> key = m_spot_lights[0];
        Eigen::Vector3f dir_movement;
        dir_movement<<0.5, 0.6, 0.5;
        dir_movement=dir_movement.normalized();
        key->set_lookat(centroid);
        key->set_position(centroid+dir_movement*3*scale); //move the light starting from the center in the direction by a certain amout so that in engulfs the whole scene
        key->m_near=( (centroid-key->position()).norm()*0.1 ) ;
        key->m_far=( (centroid-key->position()).norm()*10 ) ;
        key->m_fov=40;
        if (std::isnan(key->m_power) ){
            key->set_power_for_point(centroid, 3); //sets the power so that the lookatpoint, after attenuating, gets a certain intesity
        }
        if (!key->m_color.allFinite()){
            // key->m_color<< 255.0/255.0, 185.0/255.0, 100/255.0;
            key->m_color<< 255.0/255.0, 221.0/255.0, 180/255.0;
        }
    }
    //fill light
    if(m_spot_lights.size()>=2){
        std::shared_ptr<SpotLight> fill = m_spot_lights[1];
        Eigen::Vector3f dir_movement;
        dir_movement<< -0.5, 0.6, 0.5;
        dir_movement=dir_movement.normalized();
        fill->set_lookat(centroid);
        fill->set_position(centroid+dir_movement*3*scale); //move the light starting from the center in the direction by a certain amout so that in engulfs the whole scene
        fill->m_near=( (centroid-fill->position()).norm()*0.1 ) ;
        fill->m_far=( (centroid-fill->position()).norm()*10 ) ;
        fill->m_fov=40;
        fill->set_power_for_point(centroid, 0.8); //sets the power so that the lookatpoint, after attenuating, gets a certain intesity
        fill->m_color<< 118.0/255.0, 255.0/255.0, 230/255.0;
    }
    //rim light
    if(m_spot_lights.size()>=3){
        std::shared_ptr<SpotLight> rim = m_spot_lights[2];
        Eigen::Vector3f dir_movement;
        dir_movement<< -0.5, 0.6, -0.5;
        dir_movement=dir_movement.normalized();
        rim->set_lookat(centroid);
        rim->set_position(centroid+dir_movement*3*scale); //move the light starting from the center in the direction by a certain amout so that in engulfs the whole scene
        rim->m_near=( (centroid-rim->position()).norm()*0.1 ) ;
        rim->m_far=( (centroid-rim->position()).norm()*10 ) ;
        rim->m_fov=40;
        rim->set_power_for_point(centroid, 3); //sets the power so that the lookatpoint, after attenuating, gets a certain intesity
        // rim->m_color<< 100.0/255.0, 210.0/255.0, 255.0/255.0;
        rim->m_color<< 157.0/255.0, 227.0/255.0, 255.0/255.0;
    }
}

void Viewer::add_callback_pre_draw(const std::function<void(Viewer& viewer)> func){
    m_callbacks_pre_draw.push_back(func);
}
void Viewer::add_callback_post_draw(const std::function<void(Viewer& viewer)> func){
    m_callbacks_post_draw.push_back(func);
}

void Viewer::update(const GLuint fbo_id){
    pre_draw();
    draw(fbo_id);

    if(m_show_gui){
        m_gui->update();
    }

    post_draw();
    switch_callbacks(m_window);
}

void Viewer::pre_draw(){
    glfwPollEvents();
    if(m_show_gui){
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    // VLOG(1) << "camera is " << m_camera;
    // VLOG(1) << "camera pos is " << m_camera->position();

    for(size_t i=0; i<m_callbacks_pre_draw.size(); i++){
        m_callbacks_pre_draw[i](*this);
    }

}

void Viewer::post_draw(){

    //call any callbacks before we finish the imgui frame so the callbacks have a chance to insert some imgui code
    for(size_t i=0; i<m_callbacks_post_draw.size(); i++){
        m_callbacks_post_draw[i](*this);
    }

    //blit into the fbo_with_gui
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );
    if(m_viewport_size.x()!=m_final_fbo_with_gui.width() || m_viewport_size.y()!=m_final_fbo_with_gui.height()){
        m_final_fbo_with_gui.set_size(m_viewport_size.x(), m_viewport_size.y() );
    }
    m_final_fbo_no_gui.bind_for_read();
    m_final_fbo_with_gui.bind_for_draw();
    glBlitFramebuffer(0, 0, m_final_fbo_no_gui.width(), m_final_fbo_no_gui.height(), 0, 0, m_final_fbo_with_gui.width(),  m_final_fbo_with_gui.height(), GL_COLOR_BUFFER_BIT, GL_LINEAR);

    if(m_show_gui){
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    
    // finally just blit the final fbo to the default framebuffer
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );
    // m_final_fbo_no_gui.bind_for_read();
    m_final_fbo_with_gui.bind_for_read();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    // glBlitFramebuffer(0, 0, m_final_fbo_no_gui.width(), m_final_fbo_no_gui.height(), 0, 0, m_viewport_size.x(), m_viewport_size.y(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBlitFramebuffer(0, 0, m_final_fbo_with_gui.width(), m_final_fbo_with_gui.height(), 0, 0, m_viewport_size.x(), m_viewport_size.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);


    glfwSwapBuffers(m_window);

    // m_recorder->update();
}


void Viewer::draw(const GLuint fbo_id){

    TIME_SCOPE("draw");
    hotload_shaders();

    //GL PARAMS--------------
    if(m_enable_culling){
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }else{
        glDisable(GL_CULL_FACE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,fbo_id);
    clear_framebuffers();
    glViewport(0.0f , 0.0f, m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor );
    glEnable(GL_DEPTH_TEST);
    

    //set the camera to that it sees the whole scene 
    if(m_first_draw && !m_scene->is_empty() ){
        m_first_draw=false;
        configure_auto_params(); //automatically sets parameters that were left as "auto" in the config file
    }


    TIME_START("update_meshes");
    update_meshes_gl();
    TIME_END("update_meshes");


    TIME_START("shadow_pass");
    //loop through all the light and each mesh into their shadow maps as a depth map
    if(!m_enable_edl_lighting){
        for(size_t l_idx=0; l_idx<m_spot_lights.size(); l_idx++){
            if(m_spot_lights[l_idx]->m_create_shadow){
                m_spot_lights[l_idx]->clear_shadow_map();

                //loop through all the meshes
                for(size_t i=0; i<m_meshes_gl.size(); i++){
                    MeshGLSharedPtr mesh=m_meshes_gl[i];
                    if(mesh->m_core->m_vis.m_is_visible){

                        if(mesh->m_core->m_vis.m_show_mesh){
                            m_spot_lights[l_idx]->render_mesh_to_shadow_map(mesh);
                        }
                        if(mesh->m_core->m_vis.m_show_points){
                            m_spot_lights[l_idx]->render_points_to_shadow_map(mesh);
                        }
                    }
                }
            }
        }
    }

    TIME_END("shadow_pass");




    TIME_START("gbuffer");
    //set the gbuffer size in case it changed 
    if(m_viewport_size.x()/m_subsample_factor!=m_gbuffer.width() || m_viewport_size.y()/m_subsample_factor!=m_gbuffer.height()){
        // VLOG(1) << "m+viewpoer size is " << m_viewport_size.transpose();
        // VLOG(1) << "gbuffer has size " << m_gbuffer.width() << " " << m_gbuffer.height();
        m_gbuffer.set_size(m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor);
    }
    m_gbuffer.bind_for_draw();
    m_gbuffer.clear_depth();  //the viewer can work when we clear only the depth but for any post processing is nice to have the whole framebuffer clean
    // m_gbuffer.clear();
    TIME_END("gbuffer");


    TIME_START("geom_pass");
    glViewport(0.0f , 0.0f, m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor ); //set the viewport again because rendering the shadow maps, changed it
    //render every mesh into the gbuffer
    for(size_t i=0; i<m_meshes_gl.size(); i++){
        MeshGLSharedPtr mesh=m_meshes_gl[i];
        if(mesh->m_core->m_vis.m_is_visible){
            if(mesh->m_core->m_vis.m_show_mesh){
                render_mesh_to_gbuffer(mesh);
            }
            if(mesh->m_core->m_vis.m_show_surfels){
                render_surfels_to_gbuffer(mesh);
            }
            if(mesh->m_core->m_vis.m_show_points){
                render_points_to_gbuffer(mesh);
            }
        }
    }
    TIME_END("geom_pass");
    
    //ao_pass
    if(m_enable_ssao){
        ssao_pass();
    }else{
        // m_gbuffer.tex_with_name("position_gtex").generate_mipmap(m_ssao_downsample); //kinda hacky thing to account for possible resizes of the gbuffer and the fact that we might not have mipmaps in it. This solves the black background issue
    }
    // glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_id);


 




    //compose the final image
    compose_final_image(fbo_id);

    //blur the bloom image if we do have it
    if (m_enable_bloom){
        blur_img(m_composed_fbo.tex_with_name("bloom_gtex"), m_bloom_mip_map_lvl, m_bloom_blur_iters);
    }

    apply_postprocess();




    //attempt 3 at forward rendering 
    TIME_START("blit");
    if(m_viewport_size.x()/m_subsample_factor!=m_final_fbo_no_gui.width() || m_viewport_size.y()/m_subsample_factor!=m_final_fbo_no_gui.height()){
        m_final_fbo_no_gui.set_size(m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor  );
    }
    m_final_fbo_no_gui.bind_for_draw();
    // m_final_fbo_no_gui.clear();
    //blit the rgb from the composed_tex adn the depth from the gbuffer
    glViewport(0.0f , 0.0f, m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor );
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_posprocessed_tex.fbo_id());
    m_final_fbo_no_gui.bind_for_draw();
    // glDrawBuffer(GL_BACK);
    glBlitFramebuffer(0, 0, m_posprocessed_tex.width(), m_posprocessed_tex.height(), 0, 0, m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    //blit also the depth
    m_gbuffer.bind_for_read();
    glBlitFramebuffer( 0, 0, m_gbuffer.width(), m_gbuffer.height(), 0, 0, m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
    // glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);
    TIME_END("blit");

    //forward render the lines and edges 
    TIME_START("forward_render");
    for(size_t i=0; i<m_meshes_gl.size(); i++){
        MeshGLSharedPtr mesh=m_meshes_gl[i];
        if(mesh->m_core->m_vis.m_is_visible){
            if(mesh->m_core->m_vis.m_show_lines){
                render_lines(mesh);
            }
            if(mesh->m_core->m_vis.m_show_wireframe){
                render_wireframe(mesh);
            }
        }
    }
    TIME_END("forward_render");



    //restore state
    glDisable(GL_CULL_FACE);


}



void Viewer::update_meshes_gl(){

  

    //Check if we need to upload to gpu
    for(int i=0; i<m_scene->nr_meshes(); i++){
        MeshSharedPtr mesh_core=m_scene->get_mesh_with_idx(i);
        if(mesh_core->m_is_dirty){ //the mesh gl needs updating

            //find the meshgl  with the same name
            bool found=false;
            int idx_found=-1;
            for(size_t i = 0; i < m_meshes_gl.size(); i++){
                if(m_meshes_gl[i]->m_core->name==mesh_core->name){
                    found=true;
                    idx_found=i;
                    break;
                }
            }
            

            if(found){
                m_meshes_gl[idx_found]->assign_core(mesh_core);
                m_meshes_gl[idx_found]->upload_to_gpu();
                m_meshes_gl[idx_found]->sanity_check(); //check that we have for sure all the normals for all the vertices and faces and that everything is correct
            }else{
                MeshGLSharedPtr mesh_gpu=MeshGL::create();
                mesh_gpu->assign_core(mesh_core); //GPU implementation points to the cpu data
                mesh_core->assign_mesh_gpu(mesh_gpu); // cpu data points to the gpu implementation
                mesh_gpu->upload_to_gpu();
                mesh_gpu->sanity_check(); //check that we have for sure all the normals for all the vertices and faces and that everything is correct
                m_meshes_gl.push_back(mesh_gpu);
            }



        }        
    }


    //check if any of the mesh in the scene got deleted, in which case we should also delete the corresponding mesh_gl
    //need to do it after updating first the meshes_gl with the new meshes in the scene a some of them may have been added newly just now
    std::vector< std::shared_ptr<MeshGL> > meshes_gl_filtered;
    for(int i=0; i<m_scene->nr_meshes(); i++){
        MeshSharedPtr mesh_core=m_scene->get_mesh_with_idx(i);

        //find the mesh_gl with the same name
        bool found=false;
        for(size_t i = 0; i < m_meshes_gl.size(); i++){
            if(m_meshes_gl[i]->m_core->name==mesh_core->name){
                found=true;
                break;
            }
        }

        //we found it in the scene and in the gpu so we keep it
        if(found){
            meshes_gl_filtered.push_back(m_meshes_gl[i]);
        }
    }
    m_meshes_gl=meshes_gl_filtered;


}

void Viewer::clear_framebuffers(){
    glClearColor(m_background_color[0],
               m_background_color[1],
               m_background_color[2],
               0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}


void Viewer::render_points_to_gbuffer(const MeshGLSharedPtr mesh){

    //sanity checks 
    if( (mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticGT || mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticPred) && !mesh->m_core->m_label_mngr  ){
        LOG(WARNING) << "We are trying to show the semantic gt but we have no label manager set for this mesh";
    }

    gl::Shader& shader= m_draw_points_shader;

    // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->NV.size()){
        mesh->vao.vertex_attribute(shader, "normal", mesh->NV_buf, 3);
        shader.uniform_bool(true, "has_normals");
    }
    if(mesh->m_core->C.size()){
        GL_C(mesh->vao.vertex_attribute(shader, "color_per_vertex", mesh->C_buf, 3) );
    }
    if(mesh->m_core->I.size()){
        GL_C(mesh->vao.vertex_attribute(shader, "intensity_per_vertex", mesh->I_buf, 1) );
    }
    if(mesh->m_core->L_pred.size()){
        mesh->vao.vertex_attribute(shader, "label_pred_per_vertex", mesh->L_pred_buf, 1);
    } 
    if(mesh->m_core->L_gt.size()){
        mesh->vao.vertex_attribute(shader, "label_gt_per_vertex", mesh->L_gt_buf, 1);
    } 


    //matrices setuo
    // Eigen::Matrix4f M = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_gbuffer.width(), m_gbuffer.height());
    Eigen::Matrix4f MV = V*M;
    Eigen::Matrix4f MVP = P*V*M;
 
    //shader setup
    shader.use();
    shader.uniform_4x4(M, "M");
    shader.uniform_4x4(MV, "MV");
    shader.uniform_4x4(MVP, "MVP");
    shader.uniform_int(mesh->m_core->m_vis.m_color_type._to_integral() , "color_type");
    shader.uniform_v3_float(mesh->m_core->m_vis.m_point_color , "point_color");
    shader.uniform_float(mesh->m_core->m_vis.m_metalness , "metalness");
    shader.uniform_float(mesh->m_core->m_vis.m_roughness , "roughness");
    shader.uniform_array_v3_float(m_colormngr.viridis_colormap(), "color_scheme_height"); //for height color type
    shader.uniform_float(mesh->m_core->min_y(), "min_y");
    shader.uniform_float(mesh->m_core->max_y(), "max_y");
    if(mesh->m_core->m_label_mngr){
        shader.uniform_array_v3_float(mesh->m_core->m_label_mngr->color_scheme().cast<float>(), "color_scheme"); //for semantic labels
    }
    if(mesh->m_cur_tex_ptr->storage_initialized() ){ 
        shader.bind_texture(*mesh->m_cur_tex_ptr, "tex");
    }


    m_gbuffer.bind_for_draw();
    shader.draw_into(m_gbuffer,
                    {
                    std::make_pair("normal_out", "normal_gtex"),
                    std::make_pair("diffuse_out", "diffuse_gtex"),
                    std::make_pair("metalness_and_roughness_out", "metalness_and_roughness_gtex"),
                    }
                    ); //makes the shaders draw into the buffers we defines in the gbuffer

    glPointSize(mesh->m_core->m_vis.m_point_size);

    // draw
    mesh->vao.bind(); 
    glDrawArrays(GL_POINTS, 0, mesh->m_core->V.rows());


    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );

}

void Viewer::render_lines(const MeshGLSharedPtr mesh){

    // glEnable( GL_LINE_SMOOTH ); //draw lines antialiased (destroys performance)

    // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(m_draw_lines_shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->E.size()){
        mesh->vao.indices(mesh->E_buf); //Says the indices with we refer to vertices, this gives us the triangles
    }

    Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    Eigen::Matrix4f MVP = P*V*M;


    //shader setup
    m_draw_lines_shader.use();
    // Eigen::Matrix4f MVP=compute_mvp_matrix(mesh);
    m_draw_lines_shader.uniform_4x4(MVP, "MVP");
    m_draw_lines_shader.uniform_v3_float(mesh->m_core->m_vis.m_line_color, "line_color");
    glLineWidth( mesh->m_core->m_vis.m_line_width );

    m_draw_lines_shader.draw_into(m_final_fbo_no_gui,
                                    {
                                    // std::make_pair("position_out", "position_gtex"),
                                    std::make_pair("out_color", "color_gtex"),
                                    }
                                    ); //makes the shaders draw into the buffers we defines in the gbuffer


    // draw
    mesh->vao.bind(); 
    glDrawElements(GL_LINES, mesh->m_core->E.size(), GL_UNSIGNED_INT, 0);

    glLineWidth( 1.0f );
    
}

void Viewer::render_wireframe(const MeshGLSharedPtr mesh){

     // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(m_draw_wireframe_shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->F.size()){
        mesh->vao.indices(mesh->F_buf); //Says the indices with we refer to vertices, this gives us the triangles
    }

    Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    Eigen::Matrix4f MVP = P*V*M;


    //shader setup
    m_draw_wireframe_shader.use();
    m_draw_wireframe_shader.uniform_4x4(MVP, "MVP");

    //openglsetup
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE); //Avoid Z-buffer fighting between filled triangles & wireframe lines 
    glPolygonOffset(0.0, -5.0);
    // glEnable( GL_LINE_SMOOTH ); //draw lines antialiased (destroys performance)
    glLineWidth( mesh->m_core->m_vis.m_line_width );


    // draw
    mesh->vao.bind(); 
    glDrawElements(GL_TRIANGLES, mesh->m_core->F.size(), GL_UNSIGNED_INT, 0);


    //revert to previous openglstat
    glDisable(GL_POLYGON_OFFSET_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    // glDisable( GL_LINE_SMOOTH );
    glLineWidth( 1.0f );
    
}

void Viewer::render_mesh_to_gbuffer(const MeshGLSharedPtr mesh){

    //sanity checks 
    if( (mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticGT || mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticPred) && !mesh->m_core->m_label_mngr  ){
        LOG(WARNING) << "We are trying to show the semantic gt but we have no label manager set for this mesh";
    }

    // bool enable_solid_color=!mesh->m_core->C.size();

    // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(m_draw_mesh_shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->NV.size()){
        mesh->vao.vertex_attribute(m_draw_mesh_shader, "normal", mesh->NV_buf, 3);
    }
    if(mesh->m_core->UV.size()){
        GL_C(mesh->vao.vertex_attribute(m_draw_mesh_shader, "uv", mesh->UV_buf, 2) );
    }
    if(mesh->m_core->C.size()){
        GL_C(mesh->vao.vertex_attribute(m_draw_mesh_shader, "color_per_vertex", mesh->C_buf, 3) );
    }
    if(mesh->m_core->L_pred.size()){
        mesh->vao.vertex_attribute(m_draw_mesh_shader, "label_pred_per_vertex", mesh->L_pred_buf, 1);
    } 
    if(mesh->m_core->L_gt.size()){
        mesh->vao.vertex_attribute(m_draw_mesh_shader, "label_gt_per_vertex", mesh->L_gt_buf, 1);
    } 
    if(mesh->m_core->F.size()){
        mesh->vao.indices(mesh->F_buf); //Says the indices with we refer to vertices, this gives us the triangles
    }

    //matrices setuo
    // Eigen::Matrix4f M = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_gbuffer.width(), m_gbuffer.height());
    Eigen::Matrix4f MV = V*M;
    Eigen::Matrix4f MVP = P*V*M;
 
    //shader setup
    m_draw_mesh_shader.use();
    m_draw_mesh_shader.uniform_4x4(M, "M");
    m_draw_mesh_shader.uniform_4x4(MV, "MV");
    m_draw_mesh_shader.uniform_4x4(MVP, "MVP");
    m_draw_mesh_shader.uniform_int(mesh->m_core->m_vis.m_color_type._to_integral() , "color_type");
    m_draw_mesh_shader.uniform_v3_float(mesh->m_core->m_vis.m_solid_color , "solid_color");
    m_draw_mesh_shader.uniform_float(mesh->m_core->m_vis.m_metalness , "metalness");
    m_draw_mesh_shader.uniform_float(mesh->m_core->m_vis.m_roughness , "roughness");
    m_draw_mesh_shader.uniform_int(mesh->m_core->id , "mesh_id");
    if(mesh->m_core->m_label_mngr){
        m_draw_mesh_shader.uniform_array_v3_float(mesh->m_core->m_label_mngr->color_scheme().cast<float>(), "color_scheme"); //for semantic labels
    }
    // m_draw_mesh_shader.uniform_bool( enable_solid_color, "enable_solid_color");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_ambient_color , "ambient_color");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_core->m_vis.m_specular_color , "specular_color");
    // m_draw_mesh_shader.uniform_float(mesh->m_ambient_color_power , "ambient_color_power");
    // m_draw_mesh_shader.uniform_float(mesh->m_core->m_vis.m_shininess , "shininess");
    if(mesh->m_cur_tex_ptr->storage_initialized() ){ 
        m_draw_mesh_shader.bind_texture(*mesh->m_cur_tex_ptr, "tex");
    }

    m_gbuffer.bind_for_draw();
    m_draw_mesh_shader.draw_into(m_gbuffer,
                                    {
                                    // std::make_pair("position_out", "position_gtex"),
                                    std::make_pair("normal_out", "normal_gtex"),
                                    std::make_pair("diffuse_out", "diffuse_gtex"),
                                    std::make_pair("metalness_and_roughness_out", "metalness_and_roughness_gtex"),
                                    std::make_pair("mesh_id_out", "mesh_id_gtex"),
                                    // std::make_pair("specular_out", "specular_gtex"),
                                    // std::make_pair("shininess_out", "shininess_gtex")
                                //   std::make_pair("normal_world_out", "normal_world_gtex")
                                    }
                                    ); //makes the shaders draw into the buffers we defines in the gbuffer
    // m_draw_mesh_shader.uniform_v2_float(m_viewport_size, "viewport_size");

    // draw
    mesh->vao.bind(); 
    glDrawElements(GL_TRIANGLES, mesh->m_core->F.size(), GL_UNSIGNED_INT, 0);


    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );

}
void Viewer::render_surfels_to_gbuffer(const MeshGLSharedPtr mesh){

    //we disable surfel rendering for the moment because I changed the gbuffer diffuse texture from Half float to RGBA8 because it's a lot faster. This however means that the color accumulation cannot happen anymore in that render target. Also I didn't modify the surfel shader to output the encoded normals as per the CryEngine3 pipeline. Due to all these reasons I will disable for now the surfel rendering
    // LOG(FATAL) << "Surfel rendering disabled because we disabled the accumulation of color into the render target. this makes the rest of the program way faster. Also we would need to modify the surfel fragment shader to output encoded normals";

    // //sanity checks 
    // CHECK(mesh->m_core->V.rows()==mesh->m_core->V_tangent_u.rows() ) << "Mesh does not have tangent for each vertex. We cannot render surfels without the tangent" << mesh->m_core->V.rows() << " " << mesh->m_core->V_tangent_u.rows();
    // CHECK(mesh->m_core->V.rows()==mesh->m_core->V_length_v.rows() ) << "Mesh does not have lenght_u for each vertex. We cannot render surfels without the V_lenght_u" << mesh->m_core->V.rows() << " " << mesh->m_core->V_length_v.rows();
    // if( (mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticGT || mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticPred) && !mesh->m_core->m_label_mngr  ){
    //     LOG(WARNING) << "We are trying to show the semantic gt but we have no label manager set for this mesh";
    // }

    // // bool enable_solid_color=!mesh->m_core->C.size();

    //  // Set attributes that the vao will pulll from buffers
    // if(mesh->m_core->V.size()){
    //     mesh->vao.vertex_attribute(m_draw_surfels_shader, "position", mesh->V_buf, 3);
    // }
    // if(mesh->m_core->NV.size()){
    //     mesh->vao.vertex_attribute(m_draw_surfels_shader, "normal", mesh->NV_buf, 3);
    // }
    // if(mesh->m_core->V_tangent_u.size()){
    //     mesh->vao.vertex_attribute(m_draw_surfels_shader, "tangent_u", mesh->V_tangent_u_buf, 3);
    // }
    // if(mesh->m_core->V_length_v.size()){
    //     mesh->vao.vertex_attribute(m_draw_surfels_shader, "lenght_v", mesh->V_lenght_v_buf, 1);
    // }
    // if(mesh->m_core->C.size()){
    //     mesh->vao.vertex_attribute(m_draw_surfels_shader, "color_per_vertex", mesh->C_buf, 3);
    // }
    // if(mesh->m_core->L_pred.size()){
    //     mesh->vao.vertex_attribute(m_draw_surfels_shader, "label_pred_per_vertex", mesh->L_pred_buf, 1);
    // } 
    // if(mesh->m_core->L_gt.size()){
    //     mesh->vao.vertex_attribute(m_draw_surfels_shader, "label_gt_per_vertex", mesh->L_gt_buf, 1);
    // }

    // //matrices setuo
    // Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    // Eigen::Matrix4f V = m_camera->view_matrix();
    // Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    // Eigen::Matrix4f MV = V*M;
    // Eigen::Matrix4f MVP = P*V*M;

    // if(m_enable_surfel_splatting){
    //     glEnable(GL_BLEND);
    //     glBlendFunc(GL_ONE,GL_ONE);
    // }
    // //params
    // // glDisable(GL_DEPTH_TEST);
    // // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // // glBlendFunc(GL_SRC_ALPHA,GL_DST_ALPHA);
    // // glBlendFunc(GL_SRC_ALPHA_SATURATE,GL_DST_ALPHA);
    // // glBlendFunc(GL_SRC_ALPHA,GL_DST_ALPHA);
    // // glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    // // glBlendEquation(GL_MAX);
    // // glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE_MINUS_SRC_ALPHA);
    // // glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD); //add the rgb and alpha components
    // // glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD); //add the rgb and alpha components
 
    // //shader setup
    // if(m_gbuffer.width()!= m_viewport_size.x() || m_gbuffer.height()!=m_viewport_size.y() ){
    //     m_gbuffer.set_size(m_viewport_size.x(), m_viewport_size.y());
    // }
    // m_draw_surfels_shader.use();
    // m_draw_surfels_shader.uniform_4x4(MV, "MV");
    // m_draw_surfels_shader.uniform_4x4(MVP, "MVP");
    // m_draw_surfels_shader.uniform_int(mesh->m_core->m_vis.m_color_type._to_integral() , "color_type");
    // m_draw_surfels_shader.uniform_v3_float(mesh->m_core->m_vis.m_solid_color , "solid_color");
    // if(mesh->m_core->m_label_mngr){
    //     m_draw_surfels_shader.uniform_array_v3_float(mesh->m_core->m_label_mngr->color_scheme().cast<float>(), "color_scheme"); //for semantic labels
    // }
    // // m_draw_surfels_shader.uniform_bool( enable_solid_color , "enable_solid_color");
    // // m_draw_mesh_shader.uniform_v3_float(mesh->m_ambient_color , "ambient_color");
    // // m_draw_surfels_shader.uniform_v3_float(m_specular_color , "specular_color");
    // // m_draw_mesh_shader.uniform_float(mesh->m_ambient_color_power , "ambient_color_power");
    // // m_draw_surfels_shader.uniform_float(m_shininess , "shininess");



    // //draw only into depth map
    // m_draw_surfels_shader.uniform_bool(true , "enable_visibility_test");
    // m_draw_surfels_shader.draw_into( m_gbuffer,{} );
    // mesh->vao.bind(); 
    // glDrawArrays(GL_POINTS, 0, mesh->m_core->V.rows());



    // //now draw into the gbuffer only the ones that pass the visibility test
    // glDepthMask(false); //don't write to depth buffer but do perform the checking
    // // glEnable( GL_POLYGON_OFFSET_FILL );
    // // glPolygonOffset(m_surfel_blend_dist, m_surfel_blend_dist2); //offset the depth in the depth buffer a bit further so we can render surfels that are even a bit overlapping
    // m_draw_surfels_shader.uniform_bool(false , "enable_visibility_test");
    // m_gbuffer.bind_for_draw();
    // m_draw_surfels_shader.draw_into(m_gbuffer,
    //                                 {
    //                                 // std::make_pair("position_out", "position_gtex"),
    //                                 std::make_pair("normal_out", "normal_gtex"),
    //                                 std::make_pair("diffuse_out", "diffuse_and_weight_gtex"),
    //                                 // std::make_pair("specular_out", "specular_gtex"),
    //                                 // std::make_pair("shininess_out", "shininess_gtex")
    //                                 }
    //                                 );
    // mesh->vao.bind(); 
    // glDrawArrays(GL_POINTS, 0, mesh->m_core->V.rows());



    // GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
    // glDisable(GL_BLEND);
    // glDisable( GL_POLYGON_OFFSET_FILL );
    // glDepthMask(true);


}

void Viewer::ssao_pass(){

    //SSAO needs to perform a lot of accesses to the depth map in order to calculate occlusion. Due to cache coherency it is faster to sampler from a downsampled depth map
    //furthermore we only need the linearized depth map. So we first downsample the depthmap, then we linearize it and we run the ao shader and then the bilateral blurring

    //SETUP-------------------------
    //dont perform depth checking nor write into the depth buffer 
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);
    //viewport setup. We render into a smaller viewport so tha the ao_tex is a bit smaller
    Eigen::Vector2i new_viewport_size=calculate_mipmap_size(m_gbuffer.width(), m_gbuffer.height(), m_ssao_downsample);
    glViewport(0.0f , 0.0f, new_viewport_size.x(), new_viewport_size.y() );
    //deal with the textures
    m_ao_tex.allocate_or_resize(GL_R8, GL_RED, GL_UNSIGNED_BYTE, new_viewport_size.x(), new_viewport_size.y() ); //either fully allocates it or resizes if the size changes
    m_ao_tex.clear();
    m_gbuffer.tex_with_name("depth_gtex").generate_mipmap(m_ssao_downsample); 




    //LINEARIZE-------------------------
    TIME_START("depth_linearize_pass");
    m_depth_linear_tex.allocate_or_resize( GL_R32F, GL_RED, GL_FLOAT, new_viewport_size.x(), new_viewport_size.y() );
    m_depth_linear_tex.clear();

    // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_depth_linearize_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_depth_linearize_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles

    m_depth_linearize_shader.use();
    m_depth_linearize_shader.uniform_int(m_ssao_downsample, "pyr_lvl");
    m_depth_linearize_shader.uniform_float( m_camera->m_far / (m_camera->m_far - m_camera->m_near), "projection_a"); // according to the formula at the bottom of article https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
    m_depth_linearize_shader.uniform_float( (-m_camera->m_far * m_camera->m_near) / (m_camera->m_far - m_camera->m_near) , "projection_b");
    m_depth_linearize_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"), "depth_tex");
   
    m_depth_linearize_shader.draw_into(m_depth_linear_tex, "depth_linear_out");

    // draw
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    TIME_END("depth_linearize_pass");








    //SSAO----------------------------------------
    TIME_START("ao_pass");
    //matrix setup
    Eigen::Matrix3f V_rot = Eigen::Affine3f(m_camera->view_matrix()).linear(); //for rotating the normals from the world coords to the cam_coords
    Eigen::Matrix4f P = m_camera->proj_matrix(m_gbuffer.width(), m_gbuffer.height());
    Eigen::Matrix4f P_inv=P.inverse();


    // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_ssao_ao_pass_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_ssao_ao_pass_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles


    m_ssao_ao_pass_shader.use();
    GL_C( m_ssao_ao_pass_shader.uniform_4x4(P, "P") );
    m_ssao_ao_pass_shader.uniform_4x4(P_inv, "P_inv");
    m_ssao_ao_pass_shader.uniform_3x3(V_rot, "V_rot");
    m_ssao_ao_pass_shader.uniform_array_v3_float(m_random_samples,"random_samples");
    m_ssao_ao_pass_shader.uniform_int(m_random_samples.rows(),"nr_samples");
    m_ssao_ao_pass_shader.uniform_float(m_kernel_radius,"kernel_radius");
    // m_ssao_ao_pass_shader.uniform_int(m_ssao_downsample, "pyr_lvl"); //no need for pyramid because we only sample from depth_linear_tex which is already downsampled and has no mipmap
    // m_ssao_ao_pass_shader.bind_texture(m_depth_linear_tex,"depth_linear_tex");
    //attempt 2 with depth Not linear because using the linear depth seems to give wrong ssao when camera is near for some reason..
    m_ssao_ao_pass_shader.uniform_float( m_camera->m_far / (m_camera->m_far - m_camera->m_near), "projection_a"); // according to the formula at the bottom of article https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
    m_ssao_ao_pass_shader.uniform_float( (-m_camera->m_far * m_camera->m_near) / (m_camera->m_far - m_camera->m_near) , "projection_b");
    m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"),"depth_tex");
    m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("normal_gtex"),"normal_tex");
    m_ssao_ao_pass_shader.bind_texture(m_rvec_tex,"rvec_tex");
   
    m_ssao_ao_pass_shader.draw_into(m_ao_tex, "ao_out");

    // // draw
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    TIME_END("ao_pass");

    //restore the state
    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );





    



    // //TODO blurring should tak a mip mapped deph_linear so that we dont end up with aliasing issues
    // //bilateral blur  attempt 2 by having the ao texture also super small. we don do the blur at the full resolution because its too slow and it needs big sigma in spacial in order to work
    // TIME_START("blur_pass");
    // // m_ao_blurred_tex.allocate_or_resize( GL_R32F, GL_RED, GL_FLOAT, new_size.x(), new_size.y()  );
    // m_ao_blurred_tex.allocate_or_resize( GL_R32F, GL_RED, GL_FLOAT, new_viewport_size.x(), new_viewport_size.y()  );
    // m_depth_linear_tex.generate_mipmap(m_ssao_downsample);
    // m_bilateral_blur_shader.use();
    // m_bilateral_blur_shader.bind_texture(m_depth_linear_tex, "depth_linear_tex");
    // m_bilateral_blur_shader.bind_texture(m_ao_tex, "ao_raw_tex");
    // m_bilateral_blur_shader.uniform_float(m_sigma_spacial, "sigma_spacial");
    // m_bilateral_blur_shader.uniform_float(m_sigma_depth, "sigma_depth");
    // m_bilateral_blur_shader.uniform_int(m_ao_power, "ao_power");
    // m_bilateral_blur_shader.uniform_int(m_ssao_downsample, "pyr_lvl");
    // m_bilateral_blur_shader.bind_image(m_ao_blurred_tex, GL_WRITE_ONLY, "ao_blurred_img");
    // m_bilateral_blur_shader.dispatch(m_ao_blurred_tex.width(), m_ao_blurred_tex.height(), 16 , 16);
    // TIME_END("blur_pass");


    //dont perform depth checking nor write into the depth buffer 
    TIME_START("blur_pass");
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);

    //viewport setup. We render into a smaller viewport so tha the ao_tex is a bit smaller
    new_viewport_size=calculate_mipmap_size(m_gbuffer.width(), m_gbuffer.height(), m_ssao_downsample);
    glViewport(0.0f , 0.0f, new_viewport_size.x(), new_viewport_size.y() );
            

    // m_ao_blurred_tex.allocate_or_resize(GL_R32F, GL_RED, GL_FLOAT, new_viewport_size.x(), new_viewport_size.y() ); //either fully allocates it or resizes if the size changes
    m_ao_blurred_tex.allocate_or_resize( GL_R8, GL_RED, GL_UNSIGNED_BYTE, new_viewport_size.x(), new_viewport_size.y() );
    m_ao_blurred_tex.clear();



    //matrix setup
    Eigen::Vector2f inv_resolution;
    inv_resolution << 1.0/new_viewport_size.x(), 1.0/new_viewport_size.y();


    ///attempt 3 something is wrong with the clearing of the gbuffer
    // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_bilateral_blur_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_bilateral_blur_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles


    m_bilateral_blur_shader.use();
    m_bilateral_blur_shader.uniform_v2_float(inv_resolution, "g_InvResolutionDirection" );
    m_bilateral_blur_shader.uniform_int(m_ao_power, "ao_power");
    m_bilateral_blur_shader.uniform_float(m_sigma_spacial, "sigma_spacial");
    m_bilateral_blur_shader.uniform_float(m_sigma_depth, "sigma_depth");
    m_bilateral_blur_shader.bind_texture(m_ao_tex, "texSource");
    // m_bilateral_blur_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"),"texLinearDepth");
    m_bilateral_blur_shader.bind_texture(m_depth_linear_tex,"texLinearDepth");




    // // glColorMask(false, false, false, true);
    // m_gbuffer.bind_for_draw();
    // m_ssao_ao_pass_shader.draw_into(m_gbuffer,
    //                                 {
    //                                 // std::make_pair("position_out", "position_gtex"),
    //                                 std::make_pair("ao_out", "ao_gtex"),
    //                                 // std::make_pair("specular_out", "specular_gtex"),
    //                                 // std::make_pair("shininess_out", "shininess_gtex")
    //                                 }
    //                                 );
    m_bilateral_blur_shader.draw_into(m_ao_blurred_tex, "out_Color");



    // // draw
    // m_fullscreen_quad->vao.bind(); 
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    // glColorMask(true, true, true, true);
    TIME_END("blur_pass");

    //restore the state
    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
    // GLenum draw_buffers[1];
    // draw_buffers[0]=GL_BACK;
    // glDrawBuffers(1,draw_buffers);
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor );



}

void Viewer::compose_final_image(const GLuint fbo_id){

    TIME_START("compose");

    //create a final image the same size as the framebuffer
    // m_environment_cubemap_tex.allocate_tex_storage(GL_RGB16F, GL_RGB, GL_HALF_FLOAT, m_environment_cubemap_resolution, m_environment_cubemap_resolution);
    // m_composed_tex.allocate_or_resize(GL_RGBA16, GL_RGBA, GL_HALF_FLOAT, m_gbuffer.width(), m_gbuffer.height() );
    // m_composed_tex.set_val(m_background_color.x(), m_background_color.y(), m_background_color.z(), 0.0);

    // m_bloom_tex.allocate_or_resize(GL_RGBA16, GL_RGBA, GL_HALF_FLOAT, m_gbuffer.width(), m_gbuffer.height() );
    // m_bloom_tex.set_val(m_background_color.x(), m_background_color.y(), m_background_color.z(), 0.0);
    m_composed_fbo.set_size(m_gbuffer.width(), m_gbuffer.height() ); //established what will be the size of the textures attached to this framebuffer
    GL_C( m_composed_fbo.tex_with_name("composed_gtex").set_val(m_background_color.x(), m_background_color.y(), m_background_color.z(), 0.0) );
    // GL_C( m_composed_fbo.tex_with_name("bloom_gtex").set_val(m_background_color.x(), m_background_color.y(), m_background_color.z(), 0.0) );
    // GL_C( m_composed_fbo.sanity_check());
    // VLOG(1) << "Trying to clear the bloom_gtex";
    // GL_C( m_composed_fbo.tex_with_name("bloom_gtex").generate_mipmap(m_bloom_mip_map_lvl) );
    GL_C( m_composed_fbo.tex_with_name("bloom_gtex").clear() );
    // VLOG(1) << "finished clearing bloom gtex";


    //matrices setuo
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_gbuffer.width(), m_gbuffer.height());
    Eigen::Matrix4f P_inv = P.inverse();
    Eigen::Matrix4f V_inv = V.inverse(); //used for projecting the cam coordinates positions (which were hit with MV) stored into the gbuffer back into the world coordinates (so just makes them be affected by M which is the model matrix which just puts things into a common world coordinate)
    Eigen::Matrix3f V_inv_rot=Eigen::Affine3f(m_camera->view_matrix()).inverse().linear(); //used for getting the view rays from the cam coords to the world coords so we can sample the cubemap


    //dont perform depth checking nor write into the depth buffer 
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);

     // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles
    
    
     //shader setup
    GL_C( m_compose_final_quad_shader.use() );
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("normal_gtex"),"normal_tex");
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("diffuse_gtex"),"diffuse_tex");
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("metalness_and_roughness_gtex"),"metalness_and_roughness_tex");
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"), "depth_tex");
    if (m_show_background_img){
        m_compose_final_quad_shader.bind_texture(m_background_tex, "background_tex");
    }
    //cubemap has to be always bound otherwise the whole program crashes for some reason...
    m_compose_final_quad_shader.bind_texture(m_environment_cubemap_tex, "environment_cubemap_tex");
    m_compose_final_quad_shader.bind_texture(m_irradiance_cubemap_tex, "irradiance_cubemap_tex");
    m_compose_final_quad_shader.bind_texture(m_prefilter_cubemap_tex, "prefilter_cubemap_tex");
    m_compose_final_quad_shader.bind_texture(m_brdf_lut_tex, "brdf_lut_tex");
    if(m_enable_ssao){
        m_compose_final_quad_shader.bind_texture(m_ao_blurred_tex,"ao_tex");
        // m_compose_final_quad_shader.bind_texture(m_ao_tex,"ao_tex");
    }
    m_compose_final_quad_shader.uniform_4x4(P_inv, "P_inv");
    m_compose_final_quad_shader.uniform_4x4(V_inv, "V_inv");
    m_compose_final_quad_shader.uniform_3x3(V_inv_rot, "V_inv_rot");
    m_compose_final_quad_shader.uniform_v3_float(m_camera->position(), "eye_pos");
    m_compose_final_quad_shader.uniform_float( m_camera->m_far / (m_camera->m_far - m_camera->m_near), "projection_a"); // according to the formula at the bottom of article https://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
    m_compose_final_quad_shader.uniform_float( (-m_camera->m_far * m_camera->m_near) / (m_camera->m_far - m_camera->m_near) , "projection_b");
    m_compose_final_quad_shader.uniform_v3_float(m_ambient_color , "ambient_color");
    m_compose_final_quad_shader.uniform_float(m_ambient_color_power , "ambient_color_power");
    m_compose_final_quad_shader.uniform_bool(m_enable_ssao , "enable_ssao");
    // m_compose_final_quad_shader.uniform_float(m_shading_factor , "shading_factor");
    // m_compose_final_quad_shader.uniform_float(m_light_factor , "light_factor");
    m_compose_final_quad_shader.uniform_v2_float(m_viewport_size , "viewport_size"); //for eye dome lighing 
    m_compose_final_quad_shader.uniform_bool(m_enable_edl_lighting , "enable_edl_lighting"); //for edl lighting
    m_compose_final_quad_shader.uniform_float(m_edl_strength , "edl_strength"); //for edl lighting
    m_compose_final_quad_shader.uniform_bool(m_show_background_img , "show_background_img"); 
    m_compose_final_quad_shader.uniform_bool(m_show_environment_map, "show_environment_map");
    m_compose_final_quad_shader.uniform_bool(m_enable_ibl, "enable_ibl");
    m_compose_final_quad_shader.uniform_float(m_camera->m_exposure, "exposure");
    m_compose_final_quad_shader.uniform_bool(m_enable_bloom, "enable_bloom");
    m_compose_final_quad_shader.uniform_float(m_bloom_threshold, "bloom_threshold");


    //fill up the vector of spot lights 
    m_compose_final_quad_shader.uniform_int(m_spot_lights.size(), "nr_active_spot_lights");
    for(size_t i=0; i<m_spot_lights.size(); i++){

        Eigen::Matrix4f V_light = m_spot_lights[i]->view_matrix();
        Eigen::Vector2f viewport_size_light;
        viewport_size_light<< m_spot_lights[i]->shadow_map_resolution(), m_spot_lights[i]->shadow_map_resolution();
        Eigen::Matrix4f P_light = m_spot_lights[i]->proj_matrix(viewport_size_light);
        Eigen::Matrix4f VP = P_light*V_light; //projects the world coordinates into the light

        std::string uniform_name="spot_lights";
        //position in cam coords
        std::string uniform_pos_name =  uniform_name +"["+std::to_string(i)+"]"+".pos";
        GLint uniform_pos_loc=m_compose_final_quad_shader.get_uniform_location(uniform_pos_name);
        glUniform3fv(uniform_pos_loc, 1, m_spot_lights[i]->position().data()); 

        //color
        std::string uniform_color_name = uniform_name +"["+std::to_string(i)+"]"+".color";
        GLint uniform_color_loc=m_compose_final_quad_shader.get_uniform_location(uniform_color_name);
        glUniform3fv(uniform_color_loc, 1, m_spot_lights[i]->m_color.data()); 

        //power
        std::string uniform_power_name =  uniform_name +"["+std::to_string(i)+"]"+".power";
        GLint uniform_power_loc=m_compose_final_quad_shader.get_uniform_location(uniform_power_name);
        glUniform1f(uniform_power_loc, m_spot_lights[i]->m_power);

        //VP matrix that project world coordinates into the light 
        std::string uniform_VP_name =  uniform_name +"["+std::to_string(i)+"]"+".VP";
        GLint uniform_VP_loc=m_compose_final_quad_shader.get_uniform_location(uniform_VP_name);
        glUniformMatrix4fv(uniform_VP_loc, 1, GL_FALSE, VP.data());

        //sampler for shadow map 
        if (m_spot_lights[i]->has_shadow_map() ){ //have to check because the light might not yet have a shadow map at the start of the app when no mesh is there to be rendered
            std::string sampler_shadow_map_name =  uniform_name +"["+std::to_string(i)+"]"+".shadow_map";
            m_compose_final_quad_shader.bind_texture(m_spot_lights[i]->get_shadow_map_ref(), sampler_shadow_map_name );
        }
    }


    //make the neighbours for edl
    int neighbours_count=8; //same as in https://github.com/potree/potree/blob/65f6eb19ce7a34ce588973c262b2c3558b0f4e60/src/materials/EyeDomeLightingMaterial.js
    Eigen::MatrixXf neighbours;
    neighbours.resize(neighbours_count, 2);
    neighbours.setZero();
    for(int i=0; i<neighbours_count; i++){
        float x = std::cos(2 * i * M_PI / neighbours_count); 
        float y = std::sin(2 * i * M_PI / neighbours_count);
        neighbours.row(i) <<x,y;
    }
    // VLOG(1) << "neighbours is " << neighbours;
    m_compose_final_quad_shader.uniform_v2_float(neighbours , "neighbours");

    // m_compose_final_quad_shader.draw_into(m_composed_tex, "out_color");
    m_composed_fbo.bind_for_draw();
    m_compose_final_quad_shader.draw_into(m_composed_fbo,
                                    {
                                    // std::make_pair("position_out", "position_gtex"),
                                    std::make_pair("out_color", "composed_gtex"),
                                    std::make_pair("bloom_color", "bloom_gtex"),
                                    }
                                    ); 

    // draw
    m_fullscreen_quad->vao.bind(); 
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    TIME_END("compose");

    //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);


}

void Viewer::blur_img(gl::Texture2D& img, const int mip_map_lvl, const int m_bloom_blur_iters){

    TIME_START("blur_img");

    //first mip map the image so it's faster to blur it when it's smaller
    GL_C( img.generate_mipmap(mip_map_lvl) );

    Eigen::Vector2i blurred_img_size;
    blurred_img_size=calculate_mipmap_size(img.width(), img.height(), mip_map_lvl);
    // VLOG(1) << "blurred_img_size" << blurred_img_size.transpose();
    glViewport(0.0f , 0.0f, blurred_img_size.x(), blurred_img_size.y() );

    m_blur_tmp_tex.allocate_or_resize( img.internal_format(), img.format(), img.type(), blurred_img_size.x(), blurred_img_size.y() );
    m_blur_tmp_tex.clear();


    //dont perform depth checking nor write into the depth buffer 
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);

     // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles
    
    
    //shader setup
    GL_C( m_blur_shader.use() );


    // int iters=2;
    for (int i = 0; i < m_bloom_blur_iters; i++){

        m_blur_shader.bind_texture(img,"img");
        m_blur_shader.uniform_int(mip_map_lvl,"mip_map_lvl");
        m_blur_shader.uniform_bool(true,"horizontal");
        m_blur_shader.draw_into(m_blur_tmp_tex, "blurred_output"); 
        // draw
        m_fullscreen_quad->vao.bind(); 
        glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);


        //do it in the vertical direction
        m_blur_shader.bind_texture(m_blur_tmp_tex,"img");
        m_blur_shader.uniform_int(0,"mip_map_lvl");
        m_blur_shader.uniform_bool(false,"horizontal");
        m_blur_shader.draw_into(m_composed_fbo.tex_with_name("bloom_gtex"), "blurred_output", mip_map_lvl); 
        // draw
        m_fullscreen_quad->vao.bind(); 
        glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    }
    

    TIME_END("blur_img");

    //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x()/m_subsample_factor, m_viewport_size.y()/m_subsample_factor );


}

void Viewer::apply_postprocess(){

    TIME_START("apply_postprocess");

    //first mip map the image so it's faster to blur it when it's smaller
    // m_blur_tmp_tex.allocate_or_resize( img.internal_format(), img.format(), img.type(), m_posprocessed_tex.width(), blurred_img_size.y() );
    m_posprocessed_tex.allocate_or_resize(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, m_gbuffer.width(), m_gbuffer.height() );
    m_posprocessed_tex.set_val(m_background_color.x(), m_background_color.y(), m_background_color.z(), 0.0);



    //dont perform depth checking nor write into the depth buffer 
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);

     // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles
    
    
    //shader setup
    GL_C( m_apply_postprocess_shader.use() );



    m_apply_postprocess_shader.bind_texture(m_composed_fbo.tex_with_name("composed_gtex"),"composed_tex");
    m_apply_postprocess_shader.bind_texture(m_composed_fbo.tex_with_name("bloom_gtex"),"bloom_tex");
    m_apply_postprocess_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"), "depth_tex");
    m_apply_postprocess_shader.uniform_bool(m_show_background_img , "show_background_img"); 
    m_apply_postprocess_shader.uniform_bool(m_show_environment_map, "show_environment_map");
    m_apply_postprocess_shader.uniform_bool(m_enable_bloom, "enable_bloom");
    m_apply_postprocess_shader.uniform_int(m_bloom_mip_map_lvl,"bloom_mip_map_lvl");
    m_apply_postprocess_shader.uniform_float(m_camera->m_exposure, "exposure");
    // m_apply_postprocess_shader.uniform_v3_float(m_background_color, "background_color");
    m_apply_postprocess_shader.draw_into(m_posprocessed_tex, "out_color"); 
    // draw
    m_fullscreen_quad->vao.bind(); 
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);

   

    TIME_END("apply_postprocess");

    //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);

}
// cv::Mat Viewer::download_to_cv_mat(){
//     // glBindFramebuffer(GL_FRAMEBUFFER, 0); 
//     // int w=m_viewport_size.x()*m_subsample_factor;
//     // int h=m_viewport_size.y()*m_subsample_factor;
//     // cv::Mat cv_mat(h, w, CV_8UC4);
//     // glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, cv_mat.data);
//     // cv::Mat cv_mat_flipped;
//     // cv::flip(cv_mat, cv_mat_flipped, 0);
//     // return cv_mat_flipped;
// }

// Eigen::Matrix4f Viewer::compute_mvp_matrix(const MeshGLSharedPtr& mesh){
//     Eigen::Matrix4f M,V,P, MVP;

//     M=mesh->m_core->m_model_matrix.cast<float>().matrix();
//     V=m_camera->view_matrix();
//     P=m_camera->proj_matrix(m_viewport_size); 
//     MVP=P*V*M;
//     return MVP;
// }


void Viewer::create_random_samples_hemisphere(){
    m_random_samples.resize(m_nr_samples,3);
    for(int i=0; i<m_nr_samples; i++){ // http://john-chapman-graphics.blogspot.com/2013/01/ssao-tutorial.html
        m_random_samples.row(i) << m_rand_gen->rand_float(-1.0, 1.0),
                                   m_rand_gen->rand_float(-1.0, 1.0),
                                   m_rand_gen->rand_float(0.0 ,1.0);
        m_random_samples.row(i).normalize(); //this will make all the samples le on the SURFACE of the hemisphere. We will have to scale the samples so that 
    }
    for(int i=0; i<m_nr_samples; i++){
        float scale = float(i) / float(m_nr_samples);
        // scale = lerp(scale*scale, 0.0, 1.0, 0.1, 1.0);
        //try another form of lerp 
        scale= 0.1 + scale*scale * (1.0 - 0.1);
        m_random_samples.row(i) *= scale;
    }
}

void Viewer::load_environment_map(const std::string path){

    m_enable_ibl=true;

    read_background_img(m_background_tex, path);
    //if it's equirectangular we convert it to cubemap because it is faster to sample
    equirectangular2cubemap(m_environment_cubemap_tex, m_background_tex);
    radiance2irradiance(m_irradiance_cubemap_tex, m_environment_cubemap_tex);
    prefilter(m_prefilter_cubemap_tex, m_environment_cubemap_tex);

}

void Viewer::read_background_img(gl::Texture2D& tex, const std::string img_path){
    cv::Mat img=cv::imread(img_path, -1); //the -1 is so that it reads the image as floats because we might read a .hdr image which needs high precision
    CHECK(img.data) << "Could not open background image " << img_path;
    cv::Mat img_flipped;
    cv::flip(img, img_flipped, 0); //flip around the horizontal axis
    tex.upload_from_cv_mat(img_flipped);

}
void Viewer::equirectangular2cubemap(gl::CubeMap& cubemap_tex, const gl::Texture2D& equirectangular_tex){


    Eigen::Vector2f viewport_size;
    viewport_size<< m_environment_cubemap_resolution, m_environment_cubemap_resolution;
    glViewport(0.0f , 0.0f, viewport_size.x(), viewport_size.y() );


    //create cam
    Camera cam;
    cam.m_fov=90;
    cam.m_near=0.01;
    cam.m_far=10.0;
    cam.set_position(Eigen::Vector3f::Zero()); //camera in the middle of the NDC


    //cam matrices.
    // We supply to the shader the coordinates in clip_space. The perspective division by w will leave the coordinates unaffected therefore the NDC is the same
    //we need to revert from clip space back to a world ray so we multiply with P_inv and afterwards with V_inv (but only the rotational part because we don't want to skybox to move when we translate the camera)
    Eigen::Matrix4f P_inv;
    P_inv=cam.proj_matrix(viewport_size).inverse();

    std::vector<Eigen::Vector3f> lookat_vectors(6); //ordering of the faces is from here https://learnopengl.com/Advanced-OpenGL/Cubemaps
    lookat_vectors[0] << 1,0,0; //right
    lookat_vectors[1] << -1,0,0; //left
    lookat_vectors[2] << 0,1,0; //up
    lookat_vectors[3] << 0,-1,0; //down
    lookat_vectors[4] << 0,0,1; //backwards
    lookat_vectors[5] << 0,0,-1; //forward
    std::vector<Eigen::Vector3f> up_vectors(6); //all of the cameras have a up vector towards positive y except the ones that look at the top and bottom faces
    //TODO for some reason the up vectors had to be negated (so the camera is upside down) and only then it works. I have no idea why
    up_vectors[0] << 0,-1,0; //right
    up_vectors[1] << 0,-1,0; //left
    up_vectors[2] << 0,0,1; //up
    up_vectors[3] << 0,0,-1; //down
    up_vectors[4] << 0,-1,0; //backwards
    up_vectors[5] << 0,-1,0; //forward
   

    //render this cube 
    GL_C( glDisable(GL_CULL_FACE) );
    //dont perform depth checking nor write into the depth buffer 
    GL_C( glDepthMask(false) );
    GL_C( glDisable(GL_DEPTH_TEST) );

    gl::Shader& shader=m_equirectangular2cubemap_shader;

    // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf) ); //Says the indices with we refer to vertices, this gives us the triangles
    
    
    // //shader setup
    GL_C( shader.use() );

    for(int i=0; i<6; i++){
        //move the camera to look at the corresponding face of the cube 
        cam.set_lookat(lookat_vectors[i]); 
        cam.set_up(up_vectors[i]); 
        Eigen::Matrix3f V_inv_rot=Eigen::Affine3f(cam.view_matrix()).inverse().linear();


        shader.uniform_3x3(V_inv_rot, "V_inv_rot");
        shader.uniform_4x4(P_inv, "P_inv");
        GL_C( shader.bind_texture(equirectangular_tex,"equirectangular_tex") );
        shader.draw_into(cubemap_tex, "out_color", i);

        // draw
        GL_C( m_fullscreen_quad->vao.bind() ); 
        GL_C( glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0) );
    
    }

    // //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);



}
void Viewer::radiance2irradiance(gl::CubeMap& irradiance_tex, const gl::CubeMap& radiance_tex){

    Eigen::Vector2f viewport_size;
    viewport_size<< m_irradiance_cubemap_resolution, m_irradiance_cubemap_resolution;
    glViewport(0.0f , 0.0f, viewport_size.x(), viewport_size.y() );

   
    //create cam
    Camera cam;
    cam.m_fov=90;
    cam.m_near=0.01;
    cam.m_far=10.0;
    cam.set_position(Eigen::Vector3f::Zero()); //camera in the middle of the NDC


    //cam matrices.
    // We supply to the shader the coordinates in clip_space. The perspective division by w will leave the coordinates unaffected therefore the NDC is the same
    //we need to revert from clip space back to a world ray so we multiply with P_inv and afterwards with V_inv (but only the rotational part because we don't want to skybox to move when we translate the camera)
    Eigen::Matrix4f P_inv;
    P_inv=cam.proj_matrix(viewport_size).inverse();

    std::vector<Eigen::Vector3f> lookat_vectors(6); //ordering of the faces is from here https://learnopengl.com/Advanced-OpenGL/Cubemaps
    lookat_vectors[0] << 1,0,0; //right
    lookat_vectors[1] << -1,0,0; //left
    lookat_vectors[2] << 0,1,0; //up
    lookat_vectors[3] << 0,-1,0; //down
    lookat_vectors[4] << 0,0,1; //backwards
    lookat_vectors[5] << 0,0,-1; //forward
    std::vector<Eigen::Vector3f> up_vectors(6); //all of the cameras have a up vector towards positive y except the ones that look at the top and bottom faces
    //TODO for some reason the up vectors had to be negated (so the camera is upside down) and only then it works. I have no idea why
    up_vectors[0] << 0,-1,0; //right
    up_vectors[1] << 0,-1,0; //left
    up_vectors[2] << 0,0,1; //up
    up_vectors[3] << 0,0,-1; //down
    up_vectors[4] << 0,-1,0; //backwards
    up_vectors[5] << 0,-1,0; //forward
   

    //render this cube 
    GL_C( glDisable(GL_CULL_FACE) );
    //dont perform depth checking nor write into the depth buffer 
    GL_C( glDepthMask(false) );
    GL_C( glDisable(GL_DEPTH_TEST) );

    gl::Shader& shader=m_radiance2irradiance_shader;

    // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf) ); //Says the indices with we refer to vertices, this gives us the triangles
    
    
    // //shader setup
    GL_C( shader.use() );

    for(int i=0; i<6; i++){
        //move the camera to look at the corresponding face of the cube 
        cam.set_lookat(lookat_vectors[i]); 
        cam.set_up(up_vectors[i]); 
        Eigen::Matrix3f V_inv_rot=Eigen::Affine3f(cam.view_matrix()).inverse().linear();


        shader.uniform_3x3(V_inv_rot, "V_inv_rot");
        shader.uniform_4x4(P_inv, "P_inv");
        GL_C( shader.bind_texture(radiance_tex,"radiance_cubemap_tex") );
        shader.draw_into(irradiance_tex, "out_color", i);

        // draw
        GL_C( m_fullscreen_quad->vao.bind() ); 
        GL_C( glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0) );
    
    }

    // //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );
}
void Viewer::prefilter(gl::CubeMap& prefilter_tex, const gl::CubeMap& radiance_tex){


    //create cam
    Camera cam;
    cam.m_fov=90;
    cam.m_near=0.01;
    cam.m_far=10.0;
    cam.set_position(Eigen::Vector3f::Zero()); //camera in the middle of the NDC


    //cam matrices.
    // We supply to the shader the coordinates in clip_space. The perspective division by w will leave the coordinates unaffected therefore the NDC is the same
    //we need to revert from clip space back to a world ray so we multiply with P_inv and afterwards with V_inv (but only the rotational part because we don't want to skybox to move when we translate the camera)
    Eigen::Matrix4f P_inv;

    std::vector<Eigen::Vector3f> lookat_vectors(6); //ordering of the faces is from here https://learnopengl.com/Advanced-OpenGL/Cubemaps
    lookat_vectors[0] << 1,0,0; //right
    lookat_vectors[1] << -1,0,0; //left
    lookat_vectors[2] << 0,1,0; //up
    lookat_vectors[3] << 0,-1,0; //down
    lookat_vectors[4] << 0,0,1; //backwards
    lookat_vectors[5] << 0,0,-1; //forward
    std::vector<Eigen::Vector3f> up_vectors(6); //all of the cameras have a up vector towards positive y except the ones that look at the top and bottom faces
    //TODO for some reason the up vectors had to be negated (so the camera is upside down) and only then it works. I have no idea why
    up_vectors[0] << 0,-1,0; //right
    up_vectors[1] << 0,-1,0; //left
    up_vectors[2] << 0,0,1; //up
    up_vectors[3] << 0,0,-1; //down
    up_vectors[4] << 0,-1,0; //backwards
    up_vectors[5] << 0,-1,0; //forward
   

    //render this cube 
    GL_C( glDisable(GL_CULL_FACE) );
    //dont perform depth checking nor write into the depth buffer 
    GL_C( glDepthMask(false) );
    GL_C( glDisable(GL_DEPTH_TEST) );

    gl::Shader& shader=m_prefilter_shader;

    // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf) ); //Says the indices with we refer to vertices, this gives us the triangles
    
    
    // //shader setup
    GL_C( shader.use() );
    GL_C( shader.bind_texture(radiance_tex,"radiance_cubemap_tex") );
    shader.uniform_int(m_environment_cubemap_resolution, "radiance_cubemap_resolution");

    int mip_lvls=m_prefilter_cubemap_tex.mipmap_nr_lvls();
    for (int mip = 0; mip < mip_lvls; ++mip){
        // reisze viewport according to mip-level size.
        Eigen::Vector2f viewport_size;
        viewport_size<< m_prefilter_cubemap_resolution * std::pow(0.5, mip), m_prefilter_cubemap_resolution * std::pow(0.5, mip);
        glViewport(0.0f , 0.0f, viewport_size.x(), viewport_size.y() );


        for(int i=0; i<6; i++){
            //move the camera to look at the corresponding face of the cube 
            cam.set_lookat(lookat_vectors[i]); 
            cam.set_up(up_vectors[i]); 
            Eigen::Matrix3f V_inv_rot=Eigen::Affine3f(cam.view_matrix()).inverse().linear();
            P_inv=cam.proj_matrix(viewport_size).inverse();

            float roughness = (float)mip / (float)(mip_lvls - 1);
            shader.uniform_float(roughness, "roughness");
            shader.uniform_3x3(V_inv_rot, "V_inv_rot");
            shader.uniform_4x4(P_inv, "P_inv");
            shader.draw_into(prefilter_tex, "out_color", i, mip);

            // draw
            GL_C( m_fullscreen_quad->vao.bind() ); 
            GL_C( glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0) );
        
        }
    }

    // //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );
}
void Viewer::integrate_brdf(gl::Texture2D& brdf_lut_tex){

    TIME_START("compose");

    //dont perform depth checking nor write into the depth buffer 
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);

    Eigen::Vector2f viewport_size;
    viewport_size<< m_brdf_lut_resolution, m_brdf_lut_resolution;
    glViewport(0.0f , 0.0f, viewport_size.x(), viewport_size.y() );


    gl::Shader& shader=m_integrate_brdf_shader;    

     // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles
    
    
     //shader setup
    GL_C( shader.use() );
    shader.draw_into(brdf_lut_tex, "out_color");

    // draw
    m_fullscreen_quad->vao.bind(); 
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    TIME_END("compose");

    //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );
}


void Viewer::glfw_mouse_pressed(GLFWwindow* window, int button, int action, int modifier){
    Camera::MouseButton mb;

    if (button == GLFW_MOUSE_BUTTON_1)
        mb = Camera::MouseButton::Left;
    else if (button == GLFW_MOUSE_BUTTON_2)
        mb = Camera::MouseButton::Right;
    else //if (button == GLFW_MOUSE_BUTTON_3)
        mb = Camera::MouseButton::Middle;

    if (action == GLFW_PRESS){
        m_camera->mouse_pressed(mb,modifier);
        if(m_lights_follow_camera && m_camera==m_default_camera){
            for(size_t i=0; i<m_spot_lights.size(); i++){
                m_spot_lights[i]->mouse_pressed(mb,modifier);
            }
        }
    }
    else{
        m_camera->mouse_released(mb,modifier);
        if(m_lights_follow_camera && m_camera==m_default_camera){
            for(size_t i=0; i<m_spot_lights.size(); i++){
                m_spot_lights[i]->mouse_released(mb,modifier);
            }
        }
    }
    
}
void Viewer::glfw_mouse_move(GLFWwindow* window, double x, double y){
    m_camera->mouse_move(x, y, m_viewport_size );
    //only move if we are controlling the main camera and only if we rotating
    if(m_lights_follow_camera && m_camera==m_default_camera && m_camera->mouse_mode==Camera::MouseMode::Rotation){
        for(size_t i=0; i<m_spot_lights.size(); i++){
            m_spot_lights[i]->mouse_move(x, y, m_viewport_size );
        }
    }
        
}
void Viewer::glfw_mouse_scroll(GLFWwindow* window, double x, double y){
    m_camera->mouse_scroll(x,y);
    //do not use scroll on the lights because they will get closer to the surface and therefore appear way brither than they should be
    // if(m_lights_follow_camera && m_camera==m_default_camera){
    //     for(int i=0; i<m_spot_lights.size(); i++){
    //         m_spot_lights[i]->mouse_scroll(x,y);
    //     }
    // }
    
}
void Viewer::glfw_key(GLFWwindow* window, int key, int scancode, int action, int modifier){

    if (action == GLFW_PRESS){
        switch(key){
            case '1':{
                VLOG(1) << "pressed 1";
                if (auto mesh_gpu =  m_scene->get_mesh_with_name("mesh_test")->m_mesh_gpu.lock()) {
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_rgb_tex;
                        m_scene->get_mesh_with_name("mesh_test")->m_vis.m_color_type=MeshColorType::Texture;
                        // m_light_factor=0.0; 
                }
                break;
            }
            case '2':{
                VLOG(1) << "pressed 2";
                if (auto mesh_gpu =  m_scene->get_mesh_with_name("mesh_test")->m_mesh_gpu.lock()) {
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_tex;
                        m_scene->get_mesh_with_name("mesh_test")->m_vis.m_color_type=MeshColorType::Texture;
                        // m_light_factor=0.0; 
                }
                break;
            }
            case '3':{
                VLOG(1) << "pressed 3";
                if (auto mesh_gpu =  m_scene->get_mesh_with_name("mesh_test")->m_mesh_gpu.lock()) {
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_colored_tex;
                        m_scene->get_mesh_with_name("mesh_test")->m_vis.m_color_type=MeshColorType::Texture;
                        // m_light_factor=0.0; 
                }
                break;
            }
            case GLFW_KEY_H :{
                VLOG(1) << "toggled the main menu. Press h again for toggling";
                m_gui->toggle_main_menu();
                // m_show_gui^=1;
                break;
            }
            case GLFW_KEY_S :{
                VLOG(1) << "Snapshot";
                if(m_gui->m_record_gui){
                    m_recorder->write_without_buffering(m_final_fbo_with_gui.tex_with_name("color_gtex"), m_gui->m_snapshot_name, m_gui->m_recording_path);
                }else{
                    m_recorder->write_without_buffering(m_final_fbo_no_gui.tex_with_name("color_gtex"), m_gui->m_snapshot_name, m_gui->m_recording_path);
                }
                break;
            }

        }

    }else if(action == GLFW_RELEASE){

    }

    //handle ctrl c and ctrl v for camera pose copying and pasting 
    if (key == GLFW_KEY_C && modifier==GLFW_MOD_CONTROL && action == GLFW_PRESS){
        VLOG(1) << "Pressed ctrl-c, copying current pose of the camera to clipoard";
        glfwSetClipboardString(window, m_camera->to_string().c_str());
    }
    if (key == GLFW_KEY_V && modifier==GLFW_MOD_CONTROL && action == GLFW_PRESS){
        VLOG(1) << "Pressed ctrl-v, copying current clipboard to camera pose";
        const char* text = glfwGetClipboardString(window);
        if(text!=NULL){
            std::string pose{text};
            m_camera->from_string(pose);
        }

    }


    // __viewer->key_down(key, modifier);
    // __viewer->key_up(key, modifier);
    
}
void Viewer::glfw_char_mods(GLFWwindow* window, unsigned int codepoint, int modifier){
    
}
void Viewer::glfw_resize(GLFWwindow* window, int width, int height){
    glfwSetWindowSize(window, width, height);
    // glfwSetWindowAspectRatio(window, width, height);
    int framebuffer_width;
    int framebuffer_height;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    m_viewport_size = Eigen::Vector2f(framebuffer_width, framebuffer_height);
    // m_viewport_size = Eigen::Vector2f(width/m_subsample_factor, height/m_subsample_factor);
}

void Viewer::glfw_drop(GLFWwindow* window, int count, const char** paths){
    for(int i=0; i<count; i++){
        VLOG(1) << "loading from path " << paths[i]; 

        std::string file_ext = std::string(paths[i]).substr(std::string(paths[i]).find_last_of(".") + 1);
        trim(file_ext); //remove whitespaces from beggining and end
        if(file_ext=="hdr" || file_ext=="HDR"){
            //load environment map
            // read_background_img(m_background_tex, paths[i]);
            // equirectangular2cubemap(m_environment_cubemap_tex, m_background_tex); //if it's equirectangular we convert it to cubemap because it is faster to sample
            // radiance2irradiance(m_irradiance_cubemap_tex, m_environment_cubemap_tex);
            // prefilter(m_prefilter_cubemap_tex, m_environment_cubemap_tex);
            load_environment_map(paths[i]);
        }else{
            MeshSharedPtr mesh = Mesh::create();
            mesh->load_from_file(std::string(paths[i]));
            std::string name= "mesh_" + std::to_string(m_scene->nr_meshes());
            m_scene->add_mesh(mesh,name);
            //select the newest mesh I added 
            m_gui->select_mesh_with_idx( m_scene->nr_meshes()-1 );
        }


    }
}


// PYBIND11_MODULE(EasyPBR, m) {
//     //hacky rosinit because I cannot call rosinit from python3 because it requires installing python3-catkin-pkg and python3-rospkg which for some reason deinstalls all of melodic
//     // m.def("rosinit", []( std::string name ) {
//     //     std::vector<std::pair<std::string, std::string> > dummy_remappings;
//     //     ros::init(dummy_remappings, name);
//     //  } );

//     pybind11::class_<Viewer> (m, "Viewer") 
//     .def(pybind11::init<const std::string>())
//     .def("update", &Viewer::update, pybind11::arg("fbo_id") = 0)
//     .def_readwrite("m_gui", &Viewer::m_gui)
//     .def_readwrite("m_enable_edl_lighting", &Viewer::m_enable_edl_lighting)
//     .def_readwrite("m_enable_ssao", &Viewer::m_enable_ssao)
//     .def_readwrite("m_shading_factor", &Viewer::m_shading_factor)
//     .def_readwrite("m_light_factor", &Viewer::m_light_factor)
//     .def_readwrite("m_camera", &Viewer::m_camera)
//     .def_readwrite("m_recorder", &Viewer::m_recorder)
//     .def_readwrite("m_viewport_size", &Viewer::m_viewport_size)
//     ;

// }

