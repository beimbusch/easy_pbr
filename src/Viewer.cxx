#include "easy_pbr/Viewer.h"


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
#include "easy_pbr/Gui.h"
#include "easy_pbr/SpotLight.h"
#include "easy_pbr/Recorder.h"
// #include "MiscUtils.h"
#include "easy_pbr/LabelMngr.h"
#include "RandGenerator.h"

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

// using namespace easy_pbr::utils;

//ros
// #include "easy_pbr/utils/RosTools.h"

Viewer::Viewer(const std::string config_file):
   dummy( init_context() ),
   dummy_glad(gladLoadGL() ),
    #ifdef WITH_DIR_WATCHER 
        dir_watcher(std::string(CMAKE_SOURCE_DIR)+"/shaders/",5),
    #endif
    m_scene(new Scene),
    // m_gui(new Gui(this, m_window )),
    m_default_camera(new Camera),
    m_recorder(new Recorder(this)),
    m_rand_gen(new RandGenerator()),
    m_viewport_size(640, 480),
    m_background_color(0.2, 0.2, 0.2),
    m_draw_points_shader("draw_points"),
    m_draw_lines_shader("draw_lines"),
    m_draw_mesh_shader("draw_mesh"),
    m_draw_wireframe_shader("draw_wireframe"),
    m_rvec_tex("rvec_tex"),
    m_fullscreen_quad(MeshGLCreate()),
    m_ssao_downsample(1),
    m_nr_samples(64),
    m_kernel_radius(-1),
    m_ao_power(9),
    m_sigma_spacial(2.0),
    m_sigma_depth(0.002),
    m_ambient_color( 71.0/255.0, 70.0/255.0, 66.3/255.0  ),
    m_ambient_color_power(0.1),
    m_specular_color(77.0/255.0, 77.0/255.0, 77.0/255.0 ),
    m_shininess(14.5),
    m_enable_culling(false),
    m_enable_ssao(true),
    m_shading_factor(1.0),
    m_light_factor(1.0),
    m_surfel_blend_dist(-50),
    m_surfel_blend_dist2(0),
    m_first_draw(true)
    {
        m_camera=m_default_camera;
        init_params(config_file);
        // gladLoadGL();
    //     bool err = gladLoadGL() == 0;
    //         if (err)
    // {
    //     fprintf(stderr, "Failed to initialize OpenGL loader!\n");
    //     return ;
    // }
        // init_context();
        compile_shaders(); 
        init_opengl();                     
        m_gui=std::make_shared<Gui>(config_file, this, m_window); //needs to be initialized here because here we have done a gladloadgl
        // m_recorder->m_view=std::make_shared<Viewer>(this);
        // m_gui->init_fonts();

}

void Viewer::init_params(const std::string config_file){

    //get the config filename
    // ros::NodeHandle private_nh("~");
    // std::string config_file= getParamElseThrow<std::string>(private_nh, "config_file");
    // std::string config_file="config.cfg";

    //read all the parameters
    Config cfg = configuru::parse_file(std::string(CMAKE_SOURCE_DIR)+"/config/"+config_file, CFG);
    Config vis_config=cfg["visualization"];
    m_show_gui = vis_config["show_gui"];
    m_enable_culling = vis_config["enable_culling"];
    m_enable_ssao = vis_config["enable_ssao"];
    m_ssao_downsample = vis_config["ao_downsample"];
    m_kernel_radius = vis_config["kernel_radius"];
    m_ao_power = vis_config["ao_power"];
    m_sigma_spacial = vis_config["ao_blur_sigma_spacial"];
    m_sigma_depth = vis_config["ao_blur_sigma_depth"];
    m_shading_factor = vis_config["shading_factor"];
    m_light_factor = vis_config["light_factor"];
    m_enable_edl_lighting= vis_config["enable_edl_lighting"];
    m_edl_strength = vis_config["edl_strength"];
    m_enable_surfel_splatting = vis_config["enable_surfel_splatting"];
    m_use_background_img = vis_config["use_background_img"];
    m_background_img_path = (std::string)vis_config["background_img_path"];

    m_subsample_factor = vis_config["subsample_factor"];
    m_viewport_size/=m_subsample_factor;

    //camera stuff 
    m_camera->m_near=vis_config["camera_near"];
    m_camera->m_far=vis_config["camera_far"];

    //create the spot lights
    int nr_spot_lights=vis_config["nr_spot_lights"];
    for(int i=0; i<nr_spot_lights; i++){   
        Config light_cfg=vis_config["spot_light_"+std::to_string(i)];
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

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    m_window = glfwCreateWindow(window_width, window_height, "Renderer",nullptr,nullptr);
    if (!m_window){
        LOG(FATAL) << "GLFW window creation failed";        
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


    // window=init_context(window_width, window_height); // We initialize context here because Viewer has a lot of member variables that need a GL context
    // std::shared_ptr<Viewer> view = Viewer::create(window_width, window_height); //need to pass the size so that the viewer also knows the size of the viewport
    glfwSetWindowUserPointer(m_window, this); // so in the glfw we can acces the viewer https://stackoverflow.com/a/28660673
    setup_callbacks_viewer(m_window);

    // return window;
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
       
    GL_C( m_draw_points_shader.compile( std::string(CMAKE_SOURCE_DIR)+"/shaders/render/points_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/points_frag.glsl"  ) );
    GL_C( m_draw_points_gbuffer_shader.compile( std::string(CMAKE_SOURCE_DIR)+"/shaders/render/points_gbuffer_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/points_gbuffer_frag.glsl"  ) );
    m_draw_lines_shader.compile( std::string(CMAKE_SOURCE_DIR)+"/shaders/render/lines_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/lines_frag.glsl"  );
    m_draw_mesh_shader.compile( std::string(CMAKE_SOURCE_DIR)+"/shaders/render/mesh_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/mesh_frag.glsl"  );
    m_draw_wireframe_shader.compile( std::string(CMAKE_SOURCE_DIR)+"/shaders/render/wireframe_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/wireframe_frag.glsl"  );
    m_draw_surfels_shader.compile(std::string(CMAKE_SOURCE_DIR)+"/shaders/render/surfels_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/surfels_frag.glsl" , std::string(CMAKE_SOURCE_DIR)+"/shaders/render/surfels_geom.glsl" );
    m_compose_final_quad_shader.compile( std::string(CMAKE_SOURCE_DIR)+"/shaders/render/compose_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/render/compose_frag.glsl"  );

    // m_ssao_geom_pass_shader.compile(std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/geom_pass_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/geom_pass_frag.glsl" );
    // m_ssao_ao_pass_shader.compile(std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/ao_pass_compute.glsl");
    m_ssao_ao_pass_shader.compile(std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/ao_pass_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/ao_pass_frag.glsl" );
    m_depth_linearize_shader.compile(std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/depth_linearize_compute.glsl");
    // m_bilateral_blur_shader.compile(std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/bilateral_blur_compute.glsl");
    m_bilateral_blur_shader.compile(std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/bilateral_blur_vert.glsl", std::string(CMAKE_SOURCE_DIR)+"/shaders/ssao/bilateral_blur_frag.glsl");
}

void Viewer::init_opengl(){
    // //initialize the g buffer with some textures 
    GL_C( m_gbuffer.set_size(m_viewport_size.x(), m_viewport_size.y() ) ); //established what will be the size of the textures attached to this framebuffer
    // GL_C( m_gbuffer.add_texture("position_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) );
    // GL_C( m_gbuffer.add_texture("diffuse_gtex", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE) );
    // GL_C( m_gbuffer.add_texture("specular_gtex", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE) );
    // GL_C( m_gbuffer.add_texture("shininess_gtex", GL_R16F, GL_RED, GL_HALF_FLOAT) );
    // GL_C( m_gbuffer.add_texture("normal_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) );
    //all floats 
    // GL_C( m_gbuffer.add_texture("position_gtex", GL_RGBA32F, GL_RGBA, GL_FLOAT) );
    // GL_C( m_gbuffer.add_texture("diffuse_gtex", GL_RGBA32F, GL_RGBA, GL_FLOAT) );
    // GL_C( m_gbuffer.add_texture("specular_gtex", GL_RGBA32F, GL_RGBA, GL_FLOAT) );
    // GL_C( m_gbuffer.add_texture("shininess_gtex", GL_RGBA32F, GL_RGBA, GL_FLOAT) );
    // GL_C( m_gbuffer.add_texture("normal_gtex", GL_RGBA32F, GL_RGBA, GL_FLOAT) );
    //all half floats


    GL_C( m_gbuffer.add_texture("diffuse_gtex", GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE) ); 
    // GL_C( m_gbuffer.add_texture("diffuse_and_weight_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) ); //need to be half float because we accumulate colors for the surfels
    // GL_C( m_gbuffer.add_texture("position_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) ); //the alpha channels IS used here, if it's zero it means we have nothing covered by a mesh and the composer can discard that pixel
    //as explaine here a good packing for normals would be GL_RGB10_A2 https://community.khronos.org/t/defer-rendering-framebuffer-w-renderbuffer-help-optimizing/74230/2
    // GL_C( m_gbuffer.add_texture("normal_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) ); // the alpha channels of this one is not used by it's faster to have a 4 byte aligned one
    // GL_C( m_gbuffer.add_texture("normal_gtex", GL_RGB10_A2, GL_RGBA,  GL_UNSIGNED_INT_10_10_10_2) ); // the alpha channels of this one is not used by it's faster to have a 4 byte aligned one
    GL_C( m_gbuffer.add_texture("normal_gtex", GL_RG16F, GL_RG, GL_HALF_FLOAT) );  //as done by Cry Engine 3 in their presentation "A bit more deferred"  https://www.slideshare.net/guest11b095/a-bit-more-deferred-cry-engine3
    GL_C( m_gbuffer.add_texture("metalness_and_roughness_gtex", GL_RG8, GL_RG, GL_UNSIGNED_BYTE) ); 

    // GL_C( m_gbuffer.add_texture("normal_gtex", GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE) );  //as done by david bernard in https://hub.jmonkeyengine.org/t/solved-strange-shining-problem/32962/4 and https://github.com/davidB/jme3_ext_deferred/blob/master/src/main/resources/ShaderLib/DeferredUtils.glsllib  
    //  GL_C( m_gbuffer.add_texture("ao_gtex", GL_RG8, GL_RG, GL_UNSIGNED_BYTE) ); //stores the ao and the ao blurred in the second channel 
    // GL_C( m_gbuffer.add_texture("log_depth_gtex", GL_R32F, GL_RED, GL_FLOAT) );
    // GL_C( m_gbuffer.add_texture("log_depth_gtex", GL_R16F, GL_RED, GL_HALF_FLOAT) );
    // GL_C( m_gbuffer.add_texture("specular_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) );
    // GL_C( m_gbuffer.add_texture("shininess_gtex", GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT) );
    GL_C( m_gbuffer.add_depth("depth_gtex") );
    m_gbuffer.sanity_check();

    //set all the normal buffer to nearest because we assume that the norm of it values can be used to recover the n.z. However doing a nearest neighbour can change the norm and therefore fuck everything up
    m_gbuffer.tex_with_name("normal_gtex").set_filter_mode(GL_NEAREST);



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
    if(m_use_background_img){
        cv::Mat img=cv::imread(m_background_img_path);
        CHECK(img.data) << "Could not background image " << m_background_img_path;
        cv::Mat img_flipped;
        cv::flip(img, img_flipped, 0); //flip around the horizontal axis
        m_background_tex.upload_from_cv_mat(img_flipped);
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

}

void Viewer::post_draw(){
    if(m_show_gui){
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    glfwSwapBuffers(m_window);

    m_recorder->update();
}


void Viewer::draw(const GLuint fbo_id){




    TIME_SCOPE("draw");
    
    //set the gbuffer size in case it changed 
    if(m_viewport_size.x()==m_gbuffer.width() || m_viewport_size.y()==m_gbuffer.height()){
        m_gbuffer.set_size(m_viewport_size.x(), m_viewport_size.y());
    }
    // clear_framebuffers();

    hotload_shaders();


    if(m_enable_culling){
        // https://polycount.com/discussion/198579/2d-what-are-you-working-on-2018
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }else{
        glDisable(GL_CULL_FACE);
    }



    //set the camera to that it sees the whole scene 
    if(m_first_draw && !m_scene->is_empty() && !m_camera->m_is_initialized ){
        Eigen::Vector3f centroid = m_scene->get_centroid();
        float scale = m_scene->get_scale();

        std::cout << " get centroid " << centroid << std::endl;
    
        m_camera->set_eye(centroid);
        m_camera->push_away_by_dist(3.0*scale);

        m_first_draw=false;
        m_camera->m_is_initialized=true;
    }

    // //Check if we need to upload to gpu
    // for(int i=0; i<m_scene->get_nr_meshes(); i++){
    //     MeshSharedPtr mesh=m_scene->get_mesh_with_idx(i);
    //     if(mesh->m_is_dirty){
    //         mesh->upload_to_gpu();
    //         mesh->sanity_check(); //check that we have for sure all the normals for all the vertices and faces and that everything is correct
    //     }        
    // }
    TIME_START("update_meshes");
    update_meshes_gl();
    TIME_END("update_meshes");


    TIME_START("shadow_pass");
    //loop through all the light and each mesh into their shadow maps as a depth map
    for(int l_idx=0; l_idx<m_spot_lights.size(); l_idx++){
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
    TIME_END("shadow_pass");




    //gbuffer
    TIME_START("setup");
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );
    TIME_END("setup");

    TIME_START("gbuffer");
    m_gbuffer.bind_for_draw();
    m_gbuffer.clear();
    // m_gbuffer.tex_with_name("log_depth_gtex").set_constant(1.0);
    TIME_END("gbuffer");

    TIME_START("geom_pass");
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
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_id);


 




    //compose the final image
    glViewport(0.0f , 0.0f, m_viewport_size.x()*m_subsample_factor, m_viewport_size.y()*m_subsample_factor );
    compose_final_image(fbo_id);


    // //forward render the lines, points and edges
    //blit the depth 
    TIME_START("forward_render");
    m_gbuffer.bind_for_read();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_id); // write to default framebuffer
    glBlitFramebuffer( 0, 0, m_gbuffer.width(), m_gbuffer.height(), 0, 0, m_gbuffer.width()*m_subsample_factor, m_gbuffer.height()*m_subsample_factor, GL_DEPTH_BUFFER_BIT, GL_NEAREST );
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);

    for(size_t i=0; i<m_meshes_gl.size(); i++){
        MeshGLSharedPtr mesh=m_meshes_gl[i];
        if(mesh->m_core->m_vis.m_is_visible){
            // if(mesh->m_core->m_vis.m_show_points){
            //     render_points(mesh);
            // }
            if(mesh->m_core->m_vis.m_show_lines){
                render_lines(mesh);
            }
            if(mesh->m_core->m_vis.m_show_mesh){
                // render_mesh_to_gbuffer(mesh);
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
    for(int i=0; i<m_scene->get_nr_meshes(); i++){
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
                MeshGLSharedPtr mesh_gpu=MeshGLCreate();
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
    for(int i=0; i<m_scene->get_nr_meshes(); i++){
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


void Viewer::render_points(const MeshGLSharedPtr mesh){

    //sanity checks 
    if( (mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticGT || mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticPred) && !mesh->m_core->m_label_mngr  ){
        LOG(WARNING) << "We are trying to show the semantic gt but we have no label manager set for this mesh";
    }


    // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(m_draw_points_shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->C.size()){
        mesh->vao.vertex_attribute(m_draw_points_shader, "color_per_vertex", mesh->C_buf, 3);
    }
    if(mesh->m_core->NV.size()){ // just in case we want to show the colors corresponding to the normals
        mesh->vao.vertex_attribute(m_draw_points_shader, "normal", mesh->NV_buf, 3);
    }
    if(mesh->m_core->L_pred.size()){
        mesh->vao.vertex_attribute(m_draw_points_shader, "label_pred_per_vertex", mesh->L_pred_buf, 1);
    } 
    if(mesh->m_core->L_gt.size()){
        mesh->vao.vertex_attribute(m_draw_points_shader, "label_gt_per_vertex", mesh->L_gt_buf, 1);
    } 


    //shader setup
    m_draw_points_shader.use();
    Eigen::Matrix4f MVP=compute_mvp_matrix(mesh);
    m_draw_points_shader.uniform_4x4(MVP, "MVP");
    m_draw_points_shader.uniform_int(mesh->m_core->m_vis.m_color_type._to_integral() , "color_type");
    m_draw_points_shader.uniform_v3_float(mesh->m_core->m_vis.m_point_color, "point_color"); //for solid color
    if(mesh->m_core->m_label_mngr){
        m_draw_points_shader.uniform_array_v3_float(mesh->m_core->m_label_mngr->color_scheme().cast<float>(), "color_scheme"); //for semantic labels
    }


    glPointSize(mesh->m_core->m_vis.m_point_size);

    // draw
    mesh->vao.bind(); 
    glDrawArrays(GL_POINTS, 0, mesh->m_core->V.rows());


}

void Viewer::render_points_to_gbuffer(const MeshGLSharedPtr mesh){

    //sanity checks 
    if( (mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticGT || mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticPred) && !mesh->m_core->m_label_mngr  ){
        LOG(WARNING) << "We are trying to show the semantic gt but we have no label manager set for this mesh";
    }


    // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(m_draw_points_gbuffer_shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->NV.size()){
        mesh->vao.vertex_attribute(m_draw_points_gbuffer_shader, "normal", mesh->NV_buf, 3);
        m_draw_points_gbuffer_shader.uniform_bool(true, "has_normals");
    }
    if(mesh->m_core->C.size()){
        GL_C(mesh->vao.vertex_attribute(m_draw_points_gbuffer_shader, "color_per_vertex", mesh->C_buf, 3) );
    }
    if(mesh->m_core->I.size()){
        GL_C(mesh->vao.vertex_attribute(m_draw_points_gbuffer_shader, "intensity_per_vertex", mesh->I_buf, 1) );
    }
    if(mesh->m_core->L_pred.size()){
        mesh->vao.vertex_attribute(m_draw_points_gbuffer_shader, "label_pred_per_vertex", mesh->L_pred_buf, 1);
    } 
    if(mesh->m_core->L_gt.size()){
        mesh->vao.vertex_attribute(m_draw_points_gbuffer_shader, "label_gt_per_vertex", mesh->L_gt_buf, 1);
    } 


    //matrices setuo
    // Eigen::Matrix4f M = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    Eigen::Matrix4f MV = V*M;
    Eigen::Matrix4f MVP = P*V*M;
 
    //shader setup
    if(m_gbuffer.width()!= m_viewport_size.x() || m_gbuffer.height()!=m_viewport_size.y() ){
        m_gbuffer.set_size(m_viewport_size.x(), m_viewport_size.y());
    }
    m_draw_points_gbuffer_shader.use();
    m_draw_points_gbuffer_shader.uniform_4x4(M, "M");
    m_draw_points_gbuffer_shader.uniform_4x4(MV, "MV");
    m_draw_points_gbuffer_shader.uniform_4x4(MVP, "MVP");
    m_draw_points_gbuffer_shader.uniform_int(mesh->m_core->m_vis.m_color_type._to_integral() , "color_type");
    m_draw_points_gbuffer_shader.uniform_v3_float(mesh->m_core->m_vis.m_point_color , "point_color");
    m_draw_points_gbuffer_shader.uniform_array_v3_float(m_colormngr.viridis_colormap(), "color_scheme_height"); //for height color type
    m_draw_points_gbuffer_shader.uniform_float(mesh->m_core->min_y(), "min_y");
    m_draw_points_gbuffer_shader.uniform_float(mesh->m_core->max_y(), "max_y");
    if(mesh->m_core->m_label_mngr){
        m_draw_points_gbuffer_shader.uniform_array_v3_float(mesh->m_core->m_label_mngr->color_scheme().cast<float>(), "color_scheme"); //for semantic labels
    }
    if(mesh->m_cur_tex_ptr->get_tex_storage_initialized() ){ 
        m_draw_mesh_shader.bind_texture(*mesh->m_cur_tex_ptr, "tex");
    }


    m_gbuffer.bind_for_draw();
    m_draw_points_gbuffer_shader.draw_into(m_gbuffer,
                                    {
                                    // std::make_pair("position_out", "position_gtex"),
                                    std::make_pair("normal_out", "normal_gtex"),
                                    std::make_pair("diffuse_out", "diffuse_and_metalness_gtex"),
                                    }
                                    ); //makes the shaders draw into the buffers we defines in the gbuffer

    // m_draw_points_gbuffer_shader.draw_into(m_gbuffer,
    //                             // {std::make_pair("position_out", "position_gtex"),
    //                             {std::make_pair("normal_out", "normal_gtex")
    //                             // std::make_pair("diffuse_out", "diffuse_and_weight_gtex"),
    //                             }
    //                             ); //makes the shaders draw into the buffers we defines in the gbuffer

    glPointSize(mesh->m_core->m_vis.m_point_size);

    // draw
    mesh->vao.bind(); 
    glDrawArrays(GL_POINTS, 0, mesh->m_core->V.rows());


    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );

}

void Viewer::render_lines(const MeshGLSharedPtr mesh){

    // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(m_draw_lines_shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->E.size()){
        mesh->vao.indices(mesh->E_buf); //Says the indices with we refer to vertices, this gives us the triangles
    }


    //shader setup
    m_draw_lines_shader.use();
    Eigen::Matrix4f MVP=compute_mvp_matrix(mesh);
    m_draw_lines_shader.uniform_4x4(MVP, "MVP");
    m_draw_lines_shader.uniform_v3_float(mesh->m_core->m_vis.m_line_color, "line_color");
    glLineWidth( mesh->m_core->m_vis.m_line_width );


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


    //shader setup
    m_draw_wireframe_shader.use();
    Eigen::Matrix4f MVP=compute_mvp_matrix(mesh);
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
    Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    Eigen::Matrix4f MV = V*M;
    Eigen::Matrix4f MVP = P*V*M;
 
    //shader setup
    if(m_gbuffer.width()!= m_viewport_size.x() || m_gbuffer.height()!=m_viewport_size.y() ){
        m_gbuffer.set_size(m_viewport_size.x(), m_viewport_size.y());
    }
    m_draw_mesh_shader.use();
    m_draw_mesh_shader.uniform_4x4(M, "M");
    m_draw_mesh_shader.uniform_4x4(MV, "MV");
    m_draw_mesh_shader.uniform_4x4(MVP, "MVP");
    m_draw_mesh_shader.uniform_int(mesh->m_core->m_vis.m_color_type._to_integral() , "color_type");
    m_draw_mesh_shader.uniform_v3_float(mesh->m_core->m_vis.m_solid_color , "solid_color");
    if(mesh->m_core->m_label_mngr){
        m_draw_mesh_shader.uniform_array_v3_float(mesh->m_core->m_label_mngr->color_scheme().cast<float>(), "color_scheme"); //for semantic labels
    }
    // m_draw_mesh_shader.uniform_bool( enable_solid_color, "enable_solid_color");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_ambient_color , "ambient_color");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_core->m_vis.m_specular_color , "specular_color");
    // m_draw_mesh_shader.uniform_float(mesh->m_ambient_color_power , "ambient_color_power");
    // m_draw_mesh_shader.uniform_float(mesh->m_core->m_vis.m_shininess , "shininess");
    if(mesh->m_cur_tex_ptr->get_tex_storage_initialized() ){ 
        m_draw_mesh_shader.bind_texture(*mesh->m_cur_tex_ptr, "tex");
    }

    m_gbuffer.bind_for_draw();
    m_draw_mesh_shader.draw_into(m_gbuffer,
                                    {
                                    // std::make_pair("position_out", "position_gtex"),
                                    std::make_pair("normal_out", "normal_gtex"),
                                    std::make_pair("diffuse_out", "diffuse_gtex"),
                                    std::make_pair("metalness_and_roughness_out", "metalness_and_roughness_gtex"),
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

    //sanity checks 
    CHECK(mesh->m_core->V.rows()==mesh->m_core->V_tangent_u.rows() ) << "Mesh does not have tangent for each vertex. We cannot render surfels without the tangent" << mesh->m_core->V.rows() << " " << mesh->m_core->V_tangent_u.rows();
    CHECK(mesh->m_core->V.rows()==mesh->m_core->V_length_v.rows() ) << "Mesh does not have lenght_u for each vertex. We cannot render surfels without the V_lenght_u" << mesh->m_core->V.rows() << " " << mesh->m_core->V_length_v.rows();
    if( (mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticGT || mesh->m_core->m_vis.m_color_type==+MeshColorType::SemanticPred) && !mesh->m_core->m_label_mngr  ){
        LOG(WARNING) << "We are trying to show the semantic gt but we have no label manager set for this mesh";
    }

    // bool enable_solid_color=!mesh->m_core->C.size();

     // Set attributes that the vao will pulll from buffers
    if(mesh->m_core->V.size()){
        mesh->vao.vertex_attribute(m_draw_surfels_shader, "position", mesh->V_buf, 3);
    }
    if(mesh->m_core->NV.size()){
        mesh->vao.vertex_attribute(m_draw_surfels_shader, "normal", mesh->NV_buf, 3);
    }
    if(mesh->m_core->V_tangent_u.size()){
        mesh->vao.vertex_attribute(m_draw_surfels_shader, "tangent_u", mesh->V_tangent_u_buf, 3);
    }
    if(mesh->m_core->V_length_v.size()){
        mesh->vao.vertex_attribute(m_draw_surfels_shader, "lenght_v", mesh->V_lenght_v_buf, 1);
    }
    if(mesh->m_core->C.size()){
        mesh->vao.vertex_attribute(m_draw_surfels_shader, "color_per_vertex", mesh->C_buf, 3);
    }
    if(mesh->m_core->L_pred.size()){
        mesh->vao.vertex_attribute(m_draw_surfels_shader, "label_pred_per_vertex", mesh->L_pred_buf, 1);
    } 
    if(mesh->m_core->L_gt.size()){
        mesh->vao.vertex_attribute(m_draw_surfels_shader, "label_gt_per_vertex", mesh->L_gt_buf, 1);
    }

    //matrices setuo
    Eigen::Matrix4f M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    Eigen::Matrix4f MV = V*M;
    Eigen::Matrix4f MVP = P*V*M;

    if(m_enable_surfel_splatting){
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE,GL_ONE);
    }
    //params
    // glDisable(GL_DEPTH_TEST);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // glBlendFunc(GL_SRC_ALPHA,GL_DST_ALPHA);
    // glBlendFunc(GL_SRC_ALPHA_SATURATE,GL_DST_ALPHA);
    // glBlendFunc(GL_SRC_ALPHA,GL_DST_ALPHA);
    // glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    // glBlendEquation(GL_MAX);
    // glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE_MINUS_SRC_ALPHA);
    // glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD); //add the rgb and alpha components
    // glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD); //add the rgb and alpha components
 
    //shader setup
    if(m_gbuffer.width()!= m_viewport_size.x() || m_gbuffer.height()!=m_viewport_size.y() ){
        m_gbuffer.set_size(m_viewport_size.x(), m_viewport_size.y());
    }
    m_draw_surfels_shader.use();
    m_draw_surfels_shader.uniform_4x4(MV, "MV");
    m_draw_surfels_shader.uniform_4x4(MVP, "MVP");
    m_draw_surfels_shader.uniform_int(mesh->m_core->m_vis.m_color_type._to_integral() , "color_type");
    m_draw_surfels_shader.uniform_v3_float(mesh->m_core->m_vis.m_solid_color , "solid_color");
    if(mesh->m_core->m_label_mngr){
        m_draw_surfels_shader.uniform_array_v3_float(mesh->m_core->m_label_mngr->color_scheme().cast<float>(), "color_scheme"); //for semantic labels
    }
    // m_draw_surfels_shader.uniform_bool( enable_solid_color , "enable_solid_color");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_ambient_color , "ambient_color");
    // m_draw_surfels_shader.uniform_v3_float(m_specular_color , "specular_color");
    // m_draw_mesh_shader.uniform_float(mesh->m_ambient_color_power , "ambient_color_power");
    // m_draw_surfels_shader.uniform_float(m_shininess , "shininess");



    //draw only into depth map
    m_draw_surfels_shader.uniform_bool(true , "enable_visibility_test");
    m_draw_surfels_shader.draw_into( m_gbuffer,{} );
    mesh->vao.bind(); 
    glDrawArrays(GL_POINTS, 0, mesh->m_core->V.rows());



    //now draw into the gbuffer only the ones that pass the visibility test
    glDepthMask(false); //don't write to depth buffer but do perform the checking
    glEnable( GL_POLYGON_OFFSET_FILL );
    glPolygonOffset(m_surfel_blend_dist, m_surfel_blend_dist2); //offset the depth in the depth buffer a bit further so we can render surfels that are even a bit overlapping
    m_draw_surfels_shader.uniform_bool(false , "enable_visibility_test");
    m_gbuffer.bind_for_draw();
    m_draw_surfels_shader.draw_into(m_gbuffer,
                                    {
                                    // std::make_pair("position_out", "position_gtex"),
                                    std::make_pair("normal_out", "normal_gtex"),
                                    std::make_pair("diffuse_out", "diffuse_and_weight_gtex"),
                                    // std::make_pair("specular_out", "specular_gtex"),
                                    // std::make_pair("shininess_out", "shininess_gtex")
                                    }
                                    );
    mesh->vao.bind(); 
    glDrawArrays(GL_POINTS, 0, mesh->m_core->V.rows());



    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
    glDisable(GL_BLEND);
    glDisable( GL_POLYGON_OFFSET_FILL );
    glDepthMask(true);


}

void Viewer::ssao_pass(){

    // LOG(WARNING) << "Not working at the moment because the position cam coords tex is not used anymore. You would need to reconstruct from the depth texture";

    //subsample the positions from the gbuffer 
    TIME_START("downsample");
    m_gbuffer.tex_with_name("depth_gtex").generate_mipmap(m_ssao_downsample);
    TIME_END("downsample");

    //AO pass--------------------------

    // //TODO may be a bug here because the downsampled size by width/ssao subsample may not correspnd to the same size as in the pyramid as the mip map may do some flooring or ceeling, we need to check that
    // Eigen::Vector2i new_size=calculate_mipmap_size(m_gbuffer.width(), m_gbuffer.height(), m_ssao_downsample);
    // TIME_START("ao_pass");
    // m_ao_tex.allocate_or_resize(GL_R32F, GL_RED, GL_FLOAT, new_size.x(), new_size.y() ); //either fully allocates it or resizes if the size changes
    // Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    // Eigen::Matrix4f P_inv=P.inverse();

    // m_ssao_ao_pass_shader.use();
    // GL_C( m_ssao_ao_pass_shader.uniform_4x4(P, "proj") );
    // m_ssao_ao_pass_shader.uniform_4x4(P_inv, "P_inv");
    // m_ssao_ao_pass_shader.uniform_float(m_camera->m_near, "z_near");
    // m_ssao_ao_pass_shader.uniform_float(m_camera->m_far, "z_far");
    // m_ssao_ao_pass_shader.uniform_array_v3_float(m_random_samples,"random_samples");
    // m_ssao_ao_pass_shader.uniform_int(m_random_samples.rows(),"nr_samples");
    // m_ssao_ao_pass_shader.uniform_int(m_ssao_downsample,"pyr_lvl");
    // m_ssao_ao_pass_shader.uniform_float(m_kernel_radius,"kernel_radius");
    // m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"),"depth_tex");
    // m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("normal_gtex"),"normal_cam_coords_tex");
    // m_ssao_ao_pass_shader.bind_texture(m_rvec_tex,"rvec_tex");
    // m_ssao_ao_pass_shader.bind_image(m_ao_tex, GL_WRITE_ONLY, "ao_img");
    // m_ssao_ao_pass_shader.dispatch(m_ao_tex.width(), m_ao_tex.height(), 16 , 16);
    // TIME_END("ao_pass");


    // //ATTEMPT2
    // // Set attributes that the vao will pulll from buffers
    // GL_C( m_fullscreen_quad->vao.vertex_attribute(m_ssao_ao_pass_shader, "position", m_fullscreen_quad->V_buf, 3) );
    // GL_C( m_fullscreen_quad->vao.vertex_attribute(m_ssao_ao_pass_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    // m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles


    // m_ssao_downsample=0;
    // Eigen::Vector2i new_size=calculate_mipmap_size(m_gbuffer.width(), m_gbuffer.height(), m_ssao_downsample);
    // TIME_START("ao_pass");
    // m_ao_tex.allocate_or_resize(GL_R32F, GL_RED, GL_FLOAT, new_size.x(), new_size.y() ); //either fully allocates it or resizes if the size changes
    // Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    // Eigen::Matrix4f P_inv=P.inverse();

    // m_ssao_ao_pass_shader.use();
    // GL_C( m_ssao_ao_pass_shader.uniform_4x4(P, "P") );
    // m_ssao_ao_pass_shader.uniform_4x4(P_inv, "P_inv");
    // m_ssao_ao_pass_shader.uniform_float(m_camera->m_near, "z_near");
    // m_ssao_ao_pass_shader.uniform_float(m_camera->m_far, "z_far");
    // m_ssao_ao_pass_shader.uniform_array_v3_float(m_random_samples,"random_samples");
    // m_ssao_ao_pass_shader.uniform_int(m_random_samples.rows(),"nr_samples");
    // m_ssao_ao_pass_shader.uniform_int(m_ssao_downsample,"pyr_lvl");
    // // m_ssao_ao_pass_shader.uniform_int(0,"pyr_lvl");
    // m_ssao_ao_pass_shader.uniform_float(m_kernel_radius,"kernel_radius");
    // m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"),"depth_tex");
    // m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("normal_gtex"),"normal_cam_coords_tex");
    // m_ssao_ao_pass_shader.bind_texture(m_rvec_tex,"rvec_tex");
    // // m_ssao_ao_pass_shader.bind_image(m_ao_tex, GL_WRITE_ONLY, "ao_img");
    // // m_ssao_ao_pass_shader.dispatch(m_ao_tex.width(), m_ao_tex.height(), 16 , 16);

    // // // glColorMask(false, false, false, true);
    // m_gbuffer.bind_for_draw();
    // m_ssao_ao_pass_shader.draw_into(m_gbuffer,
    //                                 {
    //                                 // std::make_pair("position_out", "position_gtex"),
    //                                 std::make_pair("ao_out", "ao_gtex"),
    //                                 // std::make_pair("specular_out", "specular_gtex"),
    //                                 // std::make_pair("shininess_out", "shininess_gtex")
    //                                 }
    //                                 );


    // // // draw
    // // m_fullscreen_quad->vao.bind(); 
    // // glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    // // glColorMask(true, true, true, true);
    // TIME_END("ao_pass");

    // //restore 
    // GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );





    //dont perform depth checking nor write into the depth buffer 
    TIME_START("ao_pass");
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);

    //viewport setup. We render into a smaller viewport so tha the ao_tex is a bit smaller
    Eigen::Vector2i new_viewport_size=calculate_mipmap_size(m_gbuffer.width(), m_gbuffer.height(), m_ssao_downsample);
    glViewport(0.0f , 0.0f, new_viewport_size.x(), new_viewport_size.y() );
            
    //deal with the textures
    // m_ao_tex.allocate_or_resize(GL_R32F, GL_RED, GL_FLOAT, new_viewport_size.x(), new_viewport_size.y() ); //either fully allocates it or resizes if the size changes
    m_ao_tex.allocate_or_resize(GL_R8, GL_RED, GL_UNSIGNED_BYTE, new_viewport_size.x(), new_viewport_size.y() ); //either fully allocates it or resizes if the size changes
    m_ao_tex.clear();
    m_gbuffer.tex_with_name("depth_gtex").generate_mipmap(m_ssao_downsample);
    // m_gbuffer.tex_with_name("normal_gtex").generate_mipmap(m_ssao_downsample);



    //matrix setup
    Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    Eigen::Matrix4f P_inv=P.inverse();


    ///attempt 3 something is wrong with the clearing of the gbuffer
    // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_ssao_ao_pass_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_ssao_ao_pass_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles


    m_ssao_ao_pass_shader.use();
    GL_C( m_ssao_ao_pass_shader.uniform_4x4(P, "P") );
    m_ssao_ao_pass_shader.uniform_4x4(P_inv, "P_inv");
    m_ssao_ao_pass_shader.uniform_float(m_camera->m_near, "z_near");
    m_ssao_ao_pass_shader.uniform_float(m_camera->m_far, "z_far");
    m_ssao_ao_pass_shader.uniform_array_v3_float(m_random_samples,"random_samples");
    m_ssao_ao_pass_shader.uniform_int(m_random_samples.rows(),"nr_samples");
    // m_ssao_ao_pass_shader.uniform_int(m_ssao_downsample,"pyr_lvl");
    // m_ssao_ao_pass_shader.uniform_int(0,"pyr_lvl");
    m_ssao_ao_pass_shader.uniform_float(m_kernel_radius,"kernel_radius");
    m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"),"depth_tex");
    m_ssao_ao_pass_shader.bind_texture(m_gbuffer.tex_with_name("normal_gtex"),"normal_cam_coords_tex");
    m_ssao_ao_pass_shader.bind_texture(m_rvec_tex,"rvec_tex");
    // m_ssao_ao_pass_shader.bind_image(m_ao_tex, GL_WRITE_ONLY, "ao_img");
    // m_ssao_ao_pass_shader.dispatch(m_ao_tex.width(), m_ao_tex.height(), 16 , 16);




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
    m_ssao_ao_pass_shader.draw_into(m_ao_tex, "ao_out");



    // // draw
    // m_fullscreen_quad->vao.bind(); 
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    // glColorMask(true, true, true, true);
    TIME_END("ao_pass");

    //restore the state
    GL_C( glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
    // GLenum draw_buffers[1];
    // draw_buffers[0]=GL_BACK;
    // glDrawBuffers(1,draw_buffers);
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );













    //depth linearize in to perform later a bilateral blur of the ao based on depth 
    TIME_START("depth_linearize_pass");
    // m_depth_linear_tex.allocate_or_resize( GL_R32F, GL_RED, GL_FLOAT, m_gbuffer.width(), m_gbuffer.height() );
    m_depth_linear_tex.allocate_or_resize( GL_R16F, GL_RED, GL_HALF_FLOAT, m_gbuffer.width(), m_gbuffer.height() );
    m_depth_linearize_shader.use();
    m_depth_linearize_shader.uniform_float(m_camera->m_near, "z_near");
    m_depth_linearize_shader.uniform_float(m_camera->m_far, "z_far");
    m_depth_linearize_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"), "depth_tex");
    m_depth_linearize_shader.bind_image(m_depth_linear_tex, GL_WRITE_ONLY, "depth_linear_img");
    m_depth_linearize_shader.dispatch(m_gbuffer.width(), m_gbuffer.height(), 16 , 16);
    TIME_END("depth_linearize_pass");



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
    m_bilateral_blur_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"),"texLinearDepth");




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
    glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );



}

void Viewer::compose_final_image(const GLuint fbo_id){

    // TIME_START("compose");
    // m_compose_final_shader.use();
    // // m_compose_final_shader.bind_texture(m_gbuffer.tex_with_name("position_gtex"),"position_cam_coords_tex");
    // m_compose_final_shader.bind_texture(m_gbuffer.tex_with_name("normal_gtex"),"normal_cam_coords_tex");
    // // m_compose_final_shader.bind_texture(m_gbuffer.tex_with_name("diffuse_gtex"),"diffuse_tex");
    // GL_C( m_compose_final_shader.bind_image(m_gbuffer.tex_with_name("final_img_gtex"), GL_WRITE_ONLY, "final_img") );
    // m_compose_final_shader.dispatch(m_gbuffer.width(), m_gbuffer.height(), 16 , 16);
    // TIME_END("compose");


    //attempt 2 to make it a bit faster 
    TIME_START("compose");

    //matrices setuo
    Eigen::Matrix4f V = m_camera->view_matrix();
    Eigen::Matrix4f P = m_camera->proj_matrix(m_viewport_size);
    Eigen::Matrix4f P_inv = P.inverse();
    Eigen::Matrix4f V_inv = V.inverse(); //used for projecting the cam coordinates positions (which were hit with MV) stored into the gbuffer back into the world coordinates (so just makes them be affected by M which is the model matrix which just puts things into a common world coordinate)

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_id); 
    clear_framebuffers();

    //dont perform depth checking nor write into the depth buffer 
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);

     // Set attributes that the vao will pulll from buffers
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "position", m_fullscreen_quad->V_buf, 3) );
    GL_C( m_fullscreen_quad->vao.vertex_attribute(m_compose_final_quad_shader, "uv", m_fullscreen_quad->UV_buf, 2) );
    m_fullscreen_quad->vao.indices(m_fullscreen_quad->F_buf); //Says the indices with we refer to vertices, this gives us the triangles
    
    
     //shader setup
    GL_C( m_compose_final_quad_shader.use() );
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("normal_gtex"),"normal_cam_coords_tex");
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("diffuse_gtex"),"diffuse_tex");
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("metalness_and_roughness_gtex"),"metalness_and_roughness_tex");
    m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("depth_gtex"), "depth_tex");
    if (m_use_background_img){
        m_compose_final_quad_shader.bind_texture(m_background_tex, "background_tex");
    }
    // m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("specular_gtex"),"specular_tex");
    // m_compose_final_quad_shader.bind_texture(m_gbuffer.tex_with_name("shininess_gtex"),"shininess_tex");
    if(m_enable_ssao){
        m_compose_final_quad_shader.bind_texture(m_ao_blurred_tex,"ao_tex");
        // m_compose_final_quad_shader.bind_texture(m_ao_tex,"ao_tex");
    }
    m_compose_final_quad_shader.uniform_4x4(P_inv, "P_inv");
    m_compose_final_quad_shader.uniform_4x4(V_inv, "V_inv");
    m_compose_final_quad_shader.uniform_4x4(V, "V");
    m_compose_final_quad_shader.uniform_float(m_camera->m_near, "z_near");
    m_compose_final_quad_shader.uniform_float(m_camera->m_far, "z_far");
    m_compose_final_quad_shader.uniform_v3_float(m_ambient_color , "ambient_color");
    m_compose_final_quad_shader.uniform_float(m_ambient_color_power , "ambient_color_power");
    m_compose_final_quad_shader.uniform_v3_float(m_specular_color , "specular_color");
    m_compose_final_quad_shader.uniform_float(m_shininess , "shininess");
    m_compose_final_quad_shader.uniform_bool(m_enable_ssao , "enable_ssao");
    m_compose_final_quad_shader.uniform_float(m_shading_factor , "shading_factor");
    m_compose_final_quad_shader.uniform_float(m_light_factor , "light_factor");
    m_compose_final_quad_shader.uniform_v2_float(m_viewport_size , "viewport_size"); //for eye dome lighing 
    m_compose_final_quad_shader.uniform_bool(m_enable_edl_lighting , "enable_edl_lighting"); //for edl lighting
    m_compose_final_quad_shader.uniform_float(m_edl_strength , "edl_strength"); //for edl lighting
    m_compose_final_quad_shader.uniform_bool(m_use_background_img , "use_background_img"); 

    //fill up the samplers for the spot lights
    // for(int i=0; i<m_spot_lights.size(); i++){
        // m_compose_final_quad_shader.bind_texture(m_spot_lights[i]->get_shadow_map_ref(), "spot_light_shadow_maps.["+std::to_string(i)+"]" );
    // }

    //fill up the vector of spot lights 
    m_compose_final_quad_shader.uniform_int(m_spot_lights.size(), "nr_active_spot_lights");
    for(int i=0; i<m_spot_lights.size(); i++){

        Eigen::Matrix4f V_light = m_spot_lights[i]->view_matrix();
        Eigen::Vector2f viewport_size_light;
        viewport_size_light<< m_spot_lights[i]->shadow_map_resolution(), m_spot_lights[i]->shadow_map_resolution();
        Eigen::Matrix4f P_light = m_spot_lights[i]->proj_matrix(viewport_size_light);
        Eigen::Matrix4f VP = P_light*V_light; //projects the world coordinates into the light

        std::string uniform_name="spot_lights";
        //position in cam coords
        std::string uniform_pos_name =  uniform_name +"["+std::to_string(i)+"]"+".pos";
        GLint uniform_pos_loc=m_compose_final_quad_shader.get_uniform_location(uniform_pos_name);
        glUniform3fv(uniform_pos_loc, 1, m_spot_lights[i]->eye().data()); 

        //lookat in cam coords
        // std::string uniform_lookat_name =  uniform_name +"["+std::to_string(i)+"]"+".lookat";
        // GLint uniform_lookat_loc=m_compose_final_quad_shader.get_uniform_location(uniform_lookat_name);
        // glUniform3fv(uniform_lookat_loc, 1, m_spot_lights[i]->lookat().data()); 

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


    // m_draw_mesh_shader.uniform_int(mesh->m_color_type._to_integral() , "color_type");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_solid_color , "solid_color");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_ambient_color , "ambient_color");
    // m_draw_mesh_shader.uniform_v3_float(mesh->m_specular_color , "specular_color");
    // m_draw_mesh_shader.uniform_float(mesh->m_ambient_color_power , "ambient_color_power");
    // m_draw_mesh_shader.uniform_float(mesh->m_shininess , "shininess");
    // m_draw_mesh_shader.bind_texture(m_ao_blurred_tex, "ao_img");
    // m_draw_mesh_shader.uniform_float(m_ssao_subsample_factor , "ssao_subsample_factor");


    // draw
    m_fullscreen_quad->vao.bind(); 
    glDrawElements(GL_TRIANGLES, m_fullscreen_quad->m_core->F.size(), GL_UNSIGNED_INT, 0);
    TIME_END("compose");

    //restore the state
    glDepthMask(true);
    glEnable(GL_DEPTH_TEST);


    // //blit 
    // TIME_START("blit");
    // m_gbuffer.bind_for_read();
    // GL_C( glReadBuffer(GL_COLOR_ATTACHMENT0 + m_gbuffer.attachment_nr("final_img_gtex")) );
    // glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // glViewport(0.0f , 0.0f, m_viewport_size.x(), m_viewport_size.y() );
    // GL_C( glBlitFramebuffer(0, 0, m_viewport_size.x(), m_viewport_size.y(), 0, 0, m_viewport_size.x(), m_viewport_size.y(), GL_COLOR_BUFFER_BIT,    GL_NEAREST) );
    // TIME_END("blit");

}


Eigen::Matrix4f Viewer::compute_mvp_matrix(const MeshGLSharedPtr& mesh){
    Eigen::Matrix4f M,V,P, MVP;

    // M.setIdentity();
    M=mesh->m_core->m_model_matrix.cast<float>().matrix();
    V=m_camera->view_matrix();
    P=m_camera->proj_matrix(m_viewport_size); 
    // VLOG(1) << "View matrix is \n" << V;
    MVP=P*V*M;
    return MVP;
}


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
    


void Viewer::glfw_mouse_pressed(GLFWwindow* window, int button, int action, int modifier){
    Camera::MouseButton mb;

    if (button == GLFW_MOUSE_BUTTON_1)
        mb = Camera::MouseButton::Left;
    else if (button == GLFW_MOUSE_BUTTON_2)
        mb = Camera::MouseButton::Right;
    else //if (button == GLFW_MOUSE_BUTTON_3)
        mb = Camera::MouseButton::Middle;

    if (action == GLFW_PRESS)
        m_camera->mouse_pressed(mb,modifier);
    else
        m_camera->mouse_released(mb,modifier);
    
}
void Viewer::glfw_mouse_move(GLFWwindow* window, double x, double y){
    m_camera->mouse_move(x, y, m_viewport_size*m_subsample_factor );
}
void Viewer::glfw_mouse_scroll(GLFWwindow* window, double x, double y){
    m_camera->mouse_scroll(x,y);
    
}
void Viewer::glfw_key(GLFWwindow* window, int key, int scancode, int action, int modifier){

    if (action == GLFW_PRESS){
        switch(key){
            case '1':{
                VLOG(1) << "pressed 1";
                if (auto mesh_gpu =  m_scene->get_mesh_with_name("mesh_test")->m_mesh_gpu.lock()) {
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_rgb_tex;
                        m_scene->get_mesh_with_name("mesh_test")->m_vis.m_color_type=MeshColorType::Texture;
                        m_light_factor=0.0; 
                }
                break;
            }
            case '2':{
                VLOG(1) << "pressed 2";
                if (auto mesh_gpu =  m_scene->get_mesh_with_name("mesh_test")->m_mesh_gpu.lock()) {
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_tex;
                        m_scene->get_mesh_with_name("mesh_test")->m_vis.m_color_type=MeshColorType::Texture;
                        m_light_factor=0.0; 
                }
                break;
            }
            case '3':{
                VLOG(1) << "pressed 3";
                if (auto mesh_gpu =  m_scene->get_mesh_with_name("mesh_test")->m_mesh_gpu.lock()) {
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_colored_tex;
                        m_scene->get_mesh_with_name("mesh_test")->m_vis.m_color_type=MeshColorType::Texture;
                        m_light_factor=0.0; 
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
    m_viewport_size = Eigen::Vector2f(framebuffer_width/m_subsample_factor, framebuffer_height/m_subsample_factor);
    // m_viewport_size = Eigen::Vector2f(width/m_subsample_factor, height/m_subsample_factor);
}

void Viewer::glfw_drop(GLFWwindow* window, int count, const char** paths){
    for(int i=0; i<count; i++){
       VLOG(1) << "loading mesh from path " << paths[i]; 
       MeshSharedPtr mesh = MeshCreate();
       mesh->load_from_file(std::string(paths[i]));
       std::string name= "mesh_" + std::to_string(m_scene->get_nr_meshes());
       m_scene->add_mesh(mesh,name);
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
