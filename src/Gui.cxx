#include "easy_pbr/Gui.h"


//c++
#include <iostream>
#include <iomanip> // setprecision

//My stuff
#include "Profiler.h"
// #include "MiscUtils.h"
// #include "easy_pbr/core/Core.h"
// #include "easy_pbr/data_loader/DataLoaderPNG.h"
// #include "easy_pbr/data_loader/RosBagPlayer.h"
#include "easy_pbr/Viewer.h"
#include "easy_pbr/MeshGL.h"
#include "easy_pbr/Scene.h"
#include "easy_pbr/Camera.h"
#include "easy_pbr/SpotLight.h"
#include "easy_pbr/Recorder.h"
#include "string_utils.h"
// #ifdef WITH_TORCH
//     #include "easy_pbr/cnn/CNN.h"
// #endif

//imgui
#include "imgui_impl_glfw.h"
#include "imgui_ext/curve.hpp"
#include "imgui_ext/ImGuiUtils.h"

//ros
// #include <ros/ros.h>
// #include "easy_pbr/utils/RosTools.h"

using namespace easy_pbr::utils;

//loguru
//#include <loguru.hpp>

//configuru
#define CONFIGURU_WITH_EIGEN 1
#define CONFIGURU_IMPLICIT_CONVERSIONS 1
#include <configuru.hpp>
using namespace configuru;

//redeclared things here so we can use them from this file even though they are static
std::unordered_map<std::string, cv::Mat>  Gui::m_cv_mats_map;
std::unordered_map<std::string, bool>  Gui::m_cv_mats_dirty_map;
std::mutex  Gui::m_add_cv_mats_mutex;


Gui::Gui( const std::string config_file,
         Viewer* view,
         GLFWwindow* window
         ) :
        m_show_demo_window(false),
        m_show_profiler_window(true),
        m_show_player_window(true),
        m_selected_mesh_idx(0),
        m_selected_spot_light_idx(0),
        m_mesh_tex_idx(0),
        m_show_debug_textures(false),
        m_guizmo_operation(ImGuizmo::TRANSLATE),
        m_guizmo_mode(ImGuizmo::LOCAL),
        m_hidpi_scaling(1.0),
        m_subsample_factor(0.5),
        m_decimate_nr_target_faces(100)
         {
    // m_core = core;
    m_view = view;

    init_params(config_file);

    m_imgui_context = ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    const char* glsl_version = "#version 440";
    ImGui_ImplOpenGL3_Init(glsl_version);

    init_style();

    // init_fonts();
    //fonts and dpi things
    float font_size=13;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->AddFontFromFileTTF(PROGGY_DIR, font_size * m_hidpi_scaling);
    ImFontConfig config;
    config.MergeMode = true;
    const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromFileTTF(AWESOMEFONT_DIR, 13.0f*m_hidpi_scaling, &config, icon_ranges );
    io.Fonts->Build();
    //io.FontGlobalScale = 1.0 / pixel_ratio;
    ImGuiStyle *style = &ImGui::GetStyle();
    style->ScaleAllSizes(m_hidpi_scaling);

    foo[0].x = -1;

}

void Gui::init_params(const std::string config_file){
    //get the config filename
    // ros::NodeHandle private_nh("~");
    // std::string config_file= getParamElseThrow<std::string>(private_nh, "config_file");
    //  std::string config_file="config.cfg";

    //read all the parameters
    Config cfg = configuru::parse_file(std::string(CMAKE_SOURCE_DIR)+"/config/"+config_file, CFG);
    Config core_config=cfg["core"];
    bool is_hidpi = core_config["hidpi"];
    m_hidpi_scaling= is_hidpi ? 2.0 : 1.0;
}

void Gui::update() {
    // std::cout << "StaticTest" <<StaticTest::get() << '\n';
    show_images();

    ImVec2 canvas_size = ImGui::GetIO().DisplaySize;

    ImGuiWindowFlags main_window_flags = 0;
    main_window_flags |= ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowSize(ImVec2(310*m_hidpi_scaling, canvas_size.y));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("Menu", nullptr, main_window_flags);
    ImGui::PushItemWidth(135*m_hidpi_scaling);

    draw_overlays();

  




    if (ImGui::CollapsingHeader("Viewer") ) {
        //combo of the data list with names for each of them


        if(ImGui::ListBoxHeader("Scene meshes", m_view->m_meshes_gl.size(), 6)){
            for (int i = 0; i < (int)m_view->m_meshes_gl.size(); i++) {

                //it's the one we have selected so we change the header color to a whiter value
                if(i==m_selected_mesh_idx){
                    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
                }

                //visibility changes the text color from green to red
                if(m_view->m_meshes_gl[i]->m_core->m_vis.m_is_visible){
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.7f, 0.1f, 1.00f));  //green text
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.1f, 0.1f, 1.00f)); //red text
                }



                if(ImGui::Selectable(m_view->m_meshes_gl[i]->m_core->name.c_str(), true, ImGuiSelectableFlags_AllowDoubleClick)){ //we leave selected to true so that the header appears and we can change it's colors
                    if (ImGui::IsMouseDoubleClicked(0)){
                        m_view->m_meshes_gl[i]->m_core->m_vis.m_is_visible=!m_view->m_meshes_gl[i]->m_core->m_vis.m_is_visible;
                    }
                    m_selected_mesh_idx=i;
                }


                ImGui::PopStyleColor(2);
            }
            ImGui::ListBoxFooter();
        }


        if(!m_view->m_scene->is_empty() ){ //if the scene is empty there will be no mesh to select
            ImGui::InputText("Name", m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->name );
            ImGui::Checkbox("Show points", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_show_points);
            ImGui::Checkbox("Show lines", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_show_lines);
            ImGui::Checkbox("Show mesh", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_show_mesh);
            ImGui::Checkbox("Show wireframe", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_show_wireframe);
            ImGui::Checkbox("Show surfels", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_show_surfels);
            ImGui::Checkbox("Show vert ids", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_show_vert_ids);
            ImGui::Checkbox("Show vert coords", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_show_vert_coords);
            ImGui::SliderFloat("Line width", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_line_width, 0.6f, 5.0f);
            ImGui::SliderFloat("Point size", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_point_size, 1.0f, 20.0f);

            std::string current_selected_str=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_color_type._to_string();
            MeshColorType current_selected=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_color_type;
            if (ImGui::BeginCombo("Mesh color type", current_selected_str.c_str())) { // The second parameter is the label previewed before opening the combo.
                for (size_t n = 0; n < MeshColorType::_size(); n++) {
                    bool is_selected = ( current_selected == MeshColorType::_values()[n] );
                    if (ImGui::Selectable( MeshColorType::_names()[n], is_selected)){
                        m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_color_type= MeshColorType::_values()[n]; //select this one because we clicked on it
                        //if we selected texture, set the light factor to 0
                        // if(MeshColorType::_values()[n]==+MeshColorType::Texture){
                            // m_view->m_light_factor=0.0;
                        // }else{
                            // m_view->m_light_factor=1.0;
                        // }
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();   // Set the initial focus when opening the combo (scrolling + for keyboard navigation support in the upcoming navigation branch)
                }
                ImGui::EndCombo();
            }
            //if its texture then we cna choose the texture type 
            if( ImGui::SliderInt("texture_type", &m_mesh_tex_idx, 0, 2) ){
                if (auto mesh_gpu =  m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_mesh_gpu.lock()) {
                    if(m_mesh_tex_idx==0){
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_rgb_tex;
                    }else if(m_mesh_tex_idx==1){
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_tex;
                    }else if(m_mesh_tex_idx==2){
                        mesh_gpu->m_cur_tex_ptr=mesh_gpu->m_thermal_colored_tex;
                    }
                }
            }


            ImGui::ColorEdit3("Mesh color",m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_solid_color.data());
            ImGui::ColorEdit3("Point color",m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_point_color.data());
            ImGui::ColorEdit3("Line color",m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_line_color.data());
            ImGui::ColorEdit3("Label color",m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_label_color.data());
            ImGui::SliderFloat("Metalness", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_metalness, 0.0f, 1.0f) ;
            ImGui::SliderFloat("Roughness", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_vis.m_roughness, 0.0f, 1.0f  );


            //min max in y for plotting height of point clouds
            float min_y=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y(0); //this is the real min_y of the data
            float max_y=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y(1);
            float min_y_plotting = m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y_for_plotting(0); //this is the min_y that the viewer sees when plotting the mesh
            float max_y_plotting = m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y_for_plotting(1);
            ImGui::SliderFloat("min_y", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y_for_plotting(0), min_y, std::min(max_y, max_y_plotting) );
            ImGui::SliderFloat("max_y", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y_for_plotting(1), std::max(min_y_plotting, min_y), max_y);
            // ImGui::SliderFloat("min_y", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y_for_plotting(0), min_y, max_y );
            // ImGui::SliderFloat("min_y", &m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->m_min_max_y_for_plotting(1), std::max(min_y_plotting, min_y), max_y);
        }
       

        ImGui::ColorEdit3("BG color",m_view->m_background_color.data());
        ImGui::ColorEdit3("Ambient color",m_view->m_ambient_color.data());
        ImGui::SliderFloat("Ambient power", &m_view->m_ambient_color_power, 0.0f, 1.0f);
        // ImGui::ColorEdit3("Specular color",m_view->m_specular_color.data());
        // ImGui::SliderFloat("Shading factor", &m_view->m_shading_factor, 0.0f, 1.0f);
        // ImGui::SliderFloat("Light factor", &m_view->m_light_factor, 0.0f, 1.0f);
        // ImGui::SliderFloat("Surfel blend dist", &m_view->m_surfel_blend_dist, -50.0f, 50.0f);
        // ImGui::SliderFloat("Surfel blend dist2", &m_view->m_surfel_blend_dist2, -50.0f, 50.0f);

        ImGui::Checkbox("Enable LightFollow", &m_view->m_lights_follow_camera);
        ImGui::Checkbox("Enable culling", &m_view->m_enable_culling);
        ImGui::Checkbox("Enable SSAO", &m_view->m_enable_ssao);
        ImGui::Checkbox("Enable EDL", &m_view->m_enable_edl_lighting);
        ImGui::SliderFloat("EDL strength", &m_view->m_edl_strength, 0.0f, 50.0f);


        ImGui::Separator();
        if (ImGui::CollapsingHeader("Move") && !m_view->m_scene->is_empty() ) {
            if (ImGui::RadioButton("Trans", m_guizmo_operation == ImGuizmo::TRANSLATE)) { m_guizmo_operation = ImGuizmo::TRANSLATE; } ImGui::SameLine();
            if (ImGui::RadioButton("Rot", m_guizmo_operation == ImGuizmo::ROTATE)) { m_guizmo_operation = ImGuizmo::ROTATE; } ImGui::SameLine();
            if (ImGui::RadioButton("Scale", m_guizmo_operation == ImGuizmo::SCALE)) { m_guizmo_operation = ImGuizmo::SCALE; }  // is fucked up because after rotating I cannot hover over the handles

            if (ImGui::RadioButton("Local", m_guizmo_mode == ImGuizmo::LOCAL)) { m_guizmo_mode = ImGuizmo::LOCAL; } ImGui::SameLine();
            if (ImGui::RadioButton("World", m_guizmo_mode == ImGuizmo::WORLD)) { m_guizmo_mode = ImGuizmo::WORLD; } 

            edit_transform(m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx));
        }

      
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("MeshOps")) {
        if (ImGui::Button("worldGL2worldROS")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->worldGL2worldROS();
        }
        if (ImGui::Button("worldROS2worldGL")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->worldROS2worldGL();
        }
        if (ImGui::Button("Rotate_90")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->rotate_90_x_axis();
        }
        ImGui::SliderFloat("rand subsample_factor", &m_subsample_factor, 0.0, 1.0);
        if (ImGui::Button("Rand subsample")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->random_subsample(m_subsample_factor);
        }
        int nr_faces=m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->F.rows();
        ImGui::SliderInt("decimate_nr_faces", &m_decimate_nr_target_faces, 1, nr_faces);
        if (ImGui::Button("Decimate")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->decimate(m_decimate_nr_target_faces);
        }
        if (ImGui::Button("Flip normals")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->flip_normals();
        }
        

    }



    ImGui::Separator();
    if (ImGui::CollapsingHeader("SSAO")) {
        ImGui::SliderInt("Downsample", &m_view->m_ssao_downsample, 0, 5);
        ImGui::SliderFloat("Radius", &m_view->m_kernel_radius, 0.1, 100.0);
        if( ImGui::SliderInt("Nr. samples", &m_view->m_nr_samples, 8, 255) ){
            m_view->create_random_samples_hemisphere();
        }
        ImGui::SliderInt("AO power", &m_view->m_ao_power, 1, 15);
        ImGui::SliderFloat("Sigma S", &m_view->m_sigma_spacial, 1, 12.0);
        ImGui::SliderFloat("Sigma D", &m_view->m_sigma_depth, 0.1, 5.0);

    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("Camera")) {
        //select the camera, either the defalt camera or one of the point lights 
        if(ImGui::ListBoxHeader("Enabled camera", m_view->m_spot_lights.size()+1, 6)){ //all the spot

            //push the text for the default camera
            if(m_view->m_camera==m_view->m_default_camera){
                ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
            }else{
                ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
            }  
            if(ImGui::Selectable("Default cam", true)){
                m_view->m_camera=m_view->m_default_camera;
            }
            ImGui::PopStyleColor(1);

            //push camera selection box for the spot lights
            for (int i = 0; i < (int)m_view->m_spot_lights.size(); i++) {
                if(m_view->m_camera==m_view->m_spot_lights[i]){
                    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
                }      
                if(ImGui::Selectable( ("SpotLight_"+std::to_string(i)).c_str(), true ) ){ 
                    m_view->m_camera=m_view->m_spot_lights[i];
                }
                ImGui::PopStyleColor(1);
            }
            ImGui::ListBoxFooter();
        }


        ImGui::SliderFloat("FOV", &m_view->m_camera->m_fov, 30.0, 120.0);
        ImGui::SliderFloat("near", &m_view->m_camera->m_near, 0.01, 10.0);
        ImGui::SliderFloat("far", &m_view->m_camera->m_far, 100.0, 5000.0);
        // if(ImGui::Button("Print") ){
        //     m_view->m_camera->print();
        // }
        // if ( ImGui::InputText("pose", m_camera_pose) ){
        //     m_view->m_camera->from_string(m_camera_pose);
        // }
        
    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("Lights")) {
        if(ImGui::ListBoxHeader("Selected lights", m_view->m_spot_lights.size(), 6)){ //all the spot lights

            //push light selection box for the spot lights
            for (int i = 0; i < (int)m_view->m_spot_lights.size(); i++) {
                if( m_selected_spot_light_idx == i ){
                    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0.3f, 0.3f, 0.3f, 1.00f));
                }else{
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_Header]);
                }      
                if(ImGui::Selectable( ("SpotLight_"+std::to_string(i)).c_str(), true ) ){ 
                    m_selected_spot_light_idx=i;
                }
                ImGui::PopStyleColor(1);
            }
            ImGui::ListBoxFooter();

            //modify properties
            ImGui::SliderFloat("Power", &m_view->m_spot_lights[m_selected_spot_light_idx]->m_power, 100.0, 500.0);
            ImGui::ColorEdit3("Color", m_view->m_spot_lights[m_selected_spot_light_idx]->m_color.data());


        }
        
    }

    // if(m_texturer){
    //     ImGui::Separator();
        
    //     show_gl_texture(m_texturer->thermal_colored_tex_to_bake_id(), "thermal_colored_tex", false);
    //     show_gl_texture(m_texturer->rgb_tex_to_bake_id(), "RGB_tex", false);

    //     if (ImGui::CollapsingHeader("Texturer")) {

    //         // show_gl_texture(m_texturer->shadow_map_rgb_fbo_depth_id(), "rgbo_shadow_map", true);
   

    //         if(ImGui::Button("Save thermal_colored_tex_to_bake tex") ){
    //             cv::Mat rgb_mat;
    //             rgb_mat=m_texturer->m_thermal_colored_tex_to_bake.download_to_cv_mat();

    //             cv::Mat rgb_mat_bgr;
    //             cv::cvtColor(rgb_mat, rgb_mat_bgr, CV_BGRA2RGBA);

    //             //convert to 8u
    //             rgb_mat_bgr*=255;
    //             cv::Mat rgb_mat_8;
    //             rgb_mat_bgr.convertTo(rgb_mat_8, CV_8UC1);

    //             cv::imwrite("./thermal_colored_tex_to_bake.png",rgb_mat_8);
    //         }

    //         if(ImGui::Button("Save rgb_tex_to_bake tex") ){
    //             cv::Mat rgb_mat;
    //             rgb_mat=m_texturer->m_rgb_tex_to_bake.download_to_cv_mat();

    //             //convert to 8u
    //             rgb_mat*=255;
    //             cv::Mat rgb_mat_8;
    //             rgb_mat.convertTo(rgb_mat_8, CV_8UC1);

    //             cv::imwrite("./rgb_tex_to_bake.png",rgb_mat_8);
    //         }
    //     }
    // }

    // if(m_mesher){
    //     ImGui::Separator();
    //     if (ImGui::CollapsingHeader("Mesher")) {
    //         if (ImGui::Checkbox("Create naive mesh", &m_mesher->m_compute_naive_mesh) ){
    //             m_mesher->recompute_mesh_from_last_cloud();
    //         }
    //     }
    // }


    // if(m_fire_detector){
    //     ImGui::Separator();
    //     if (ImGui::CollapsingHeader("FireDetector")) {
    //         ImGui::SliderFloat("fire_thresh", &m_fire_detector->m_fire_threshold, 0.0, 1.0);

    //         show_gl_texture(m_fire_detector->binary_tex_id(), "binary_tex", true);
    //         show_gl_texture(m_fire_detector->positions_world_tex_id(), "position_world_tex", true);
    //     }
    // }


    // if(m_latticeCPU_test){
    //     ImGui::Separator();
    //     if (ImGui::CollapsingHeader("latticeCPU_test")) {
    //         ImGui::SliderFloat("spacial_sigma", &m_latticeCPU_test->m_spacial_sigma, 0.0, 30.0);
    //         ImGui::SliderFloat("color_sigma", &m_latticeCPU_test->m_color_sigma, 0.0, 1.0);
    //     }
    // }

    // if(m_latticeGPU_test){
    //     // ImGui::Separator();
    //     // if (ImGui::CollapsingHeader("latticeGPU_test")) {
    //     //     for(int i=0; i<m_latticeGPU_test->m_lattice->m_sigmas_val_and_extent.size(); i++){
    //     //         std::string sigma_name="sigma_"+std::to_string(i);
    //     //         ImGui::SliderFloat(sigma_name.c_str(), &m_latticeGPU_test->m_lattice->m_sigmas_val_and_extent.first, 0.0, 30.0);
    //     //     }
    //     // }
    // }




    // // ImGui::Separator();
    // // if (ImGui::CollapsingHeader("Lattice")) {
    // //     ImGui::SliderFloat("spacial_sigma", &m_core->m_lattice_cpu_test->m_spacial_sigma, 1.0, 100.0);
    // //     ImGui::SliderFloat("color_sigma", &m_core->m_lattice_cpu_test->m_color_sigma, 0.01, 1.0);
    // //     ImGui::SliderFloat("surfel_scaling", &m_core->m_lattice_cpu_test->m_surfel_scaling, 0.1, 10.0);
    // // }

    // // ImGui::Separator();
    // // if (ImGui::CollapsingHeader("Segmenter")) {
    // //     ImGui::SliderFloat("spacial_sigma", &m_core->m_segmenter->m_spacial_sigma, 0.25, 100.0);
    // //     ImGui::SliderFloat("normal_sigma", &m_core->m_segmenter->m_normal_sigma, 0.01, 1.0);
    // //     ImGui::SliderFloat("min_weight", &m_core->m_segmenter->m_min_weight, 1.0, 100.0);
    // //     ImGui::SliderFloat("surfel_scaling", &m_core->m_segmenter->m_surfel_scaling, 0.1, 10.0);
    // // }

    // if(m_ball_detector){
    //     ImGui::Separator();
    //     if (ImGui::CollapsingHeader("BallDetector")) {
    //         // ImGui::Checkbox("filter_by_area", &m_ball_detector->m_params.filterByArea);
    //         // ImGui::Checkbox("filter_by_circularity", &m_ball_detector->m_params.filterByCircularity);
    //         // ImGui::Checkbox("filter_by_color", &m_ball_detector->m_params.filterByColor);
    //         // ImGui::Checkbox("filter_by_convexity", &m_ball_detector->m_params.filterByConvexity);
    //         // ImGui::Checkbox("filter_by_inertia", &m_ball_detector->m_params.filterByInertia);
    //         // ImGui::SliderFloat("maxArea", &m_ball_detector->m_params.maxArea, 0.0, 1.0 );
    //         // ImGui::SliderFloat("maxCircularity", &m_ball_detector->m_params.maxCircularity, 0.0, 1.0 );
    //         // ImGui::SliderFloat("maxConvexity", &m_ball_detector->m_params.maxConvexity, 0.0, 1.0 );
    //         // ImGui::SliderFloat("maxInertiaRatio", &m_ball_detector->m_params.maxInertiaRatio, 0.0, 1.0 );
    //         // ImGui::SliderFloat("maxThreshold", &m_ball_detector->m_params.maxThreshold, 0.0, 1.0 );
    //         // ImGui::SliderFloat("minArea", &m_ball_detector->m_params.minArea, 0.0, 1.0 );
    //         // ImGui::SliderFloat("minCircularity", &m_ball_detector->m_params.minCircularity, 0.0, 1.0 );
    //         // ImGui::SliderFloat("minConvexity", &m_ball_detector->m_params.minConvexity, 0.0, 1.0 );
    //         // ImGui::SliderFloat("minDistBetweenBlobs", &m_ball_detector->m_params.minDistBetweenBlobs, 0.0, 1.0 );
    //         // // ImGui::SliderInt("minInertiaRatio", &m_core->m_ball_detector->m_params.minRepeatability, 0, 100 );
    //         // ImGui::SliderFloat("thresholdStep", &m_ball_detector->m_params.thresholdStep, 0, 100 );

    //         ImGui::SliderFloat("thresholdStep", &m_ball_detector->m_params.thresholdStep, 0.01, 100 );
    //         ImGui::SliderFloat("minThreshold", &m_ball_detector->m_params.minThreshold, 0, 255 );
    //         ImGui::SliderFloat("maxThreshold", &m_ball_detector->m_params.maxThreshold, 0, 255 );
    //         // ImGui::SliderInt("minRepeatability", &m_ball_detector->m_params.minRepeatability, 0, 10 ); //we need a slider for long unsigned int which imgui has as datatype but I am lazy to implement it
    //         ImGui::SliderFloat("minDistBetweenBlobs", &m_ball_detector->m_params.minDistBetweenBlobs, 0, 100 );

    //         ImGui::Checkbox("filter_by_color", &m_ball_detector->m_params.filterByColor);
    //         // ImGui::SliderInt("blobColor", &m_ball_detector->m_params.blobColor, 0, 255 ); //we need a slider for uchar which imgui doesnt have

    //         ImGui::Checkbox("filter_by_area", &m_ball_detector->m_params.filterByArea);
    //         ImGui::SliderFloat("minArea", &m_ball_detector->m_params.minArea, 0.0, 500.0 );
    //         ImGui::SliderFloat("maxArea", &m_ball_detector->m_params.maxArea, 0.0, 10000.0 );

    //         ImGui::Checkbox("filter_by_circularity", &m_ball_detector->m_params.filterByCircularity);
    //         ImGui::SliderFloat("minCircularity", &m_ball_detector->m_params.minCircularity, 0.0, 1.0 );
    //         ImGui::SliderFloat("maxCircularity", &m_ball_detector->m_params.maxCircularity, 0.0, 99999 );

    //         ImGui::Checkbox("filter_by_inertia", &m_ball_detector->m_params.filterByInertia);
    //         ImGui::SliderFloat("minInertiaRatio", &m_ball_detector->m_params.minInertiaRatio, 0, 100 );
    //         ImGui::SliderFloat("maxInertiaRatio", &m_ball_detector->m_params.maxInertiaRatio, 0, 99999 );

    //         ImGui::Checkbox("filter_by_convexity", &m_ball_detector->m_params.filterByConvexity);
    //         ImGui::SliderFloat("minConvexity", &m_ball_detector->m_params.minConvexity, 0.0, 1.0 );
    //         ImGui::SliderFloat("maxConvexity", &m_ball_detector->m_params.maxConvexity, 0.0, 99999 );

    //     }
    // }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("IO")) {
        ImGui::InputText("write_mesh_path", m_write_mesh_file_path);
        if (ImGui::Button("Write mesh")){
            m_view->m_scene->get_mesh_with_idx(m_selected_mesh_idx)->save_to_file(m_write_mesh_file_path);
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Recorder")) {
        ImGui::InputText("results_path", m_view->m_recorder->m_results_path);
        ImGui::InputText("name", m_view->m_recorder->m_single_png_filename);
        if(ImGui::Button("Write viewer to png") ){
            m_view->m_recorder->write_viewer_to_png();
        }
        ImGui::SliderFloat("Magnification", &m_view->m_recorder->m_magnification, 1.0f, 5.0f);

        //recording
        ImVec2 button_size(25*m_hidpi_scaling,25*m_hidpi_scaling);
        const char* icon_recording = m_view->m_recorder->m_is_recording ? ICON_FA_PAUSE : ICON_FA_CIRCLE;
        // if(ImGui::Button("Record") ){
        if(ImGui::Button(icon_recording, button_size) ){
            m_view->m_recorder->m_is_recording^= 1;
        }
    }


    ImGui::Separator();
    if (ImGui::CollapsingHeader("Profiler")) {
        ImGui::Checkbox("Profile gpu", &Profiler_ns::m_profile_gpu);
        if (ImGui::Button("Print profiling stats")){
            Profiler_ns::Profiler::print_all_stats();
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::Checkbox("Show debug textures", &m_show_debug_textures);
    }
    if(m_show_debug_textures){
        // show_gl_texture(m_view->m_gbuffer.tex_with_name("position_gtex").get_tex_id(), "position_gtex", true);
        show_gl_texture(m_view->m_gbuffer.tex_with_name("diffuse_gtex").get_tex_id(), "diffuse_gtex", true);
        show_gl_texture(m_view->m_gbuffer.tex_with_name("normal_gtex").get_tex_id(), "normal_gtex", true);
        show_gl_texture(m_view->m_gbuffer.tex_with_name("depth_gtex").get_tex_id(), "depth_gtex", true);
        show_gl_texture(m_view->m_gbuffer.tex_with_name("metalness_and_roughness_gtex").get_tex_id(), "metalness_and_roughness_gtex", true);
        // show_gl_texture(m_view->m_gbuffer.tex_with_name("ao_gtex").get_tex_id(), "ao_gtex", true);
        // show_gl_texture(m_view->m_gbuffer.tex_with_name("log_depth_gtex").get_tex_id(), "log_depth_gtex", true);
        // show_gl_texture(m_view->m_gbuffer.tex_with_name("depth_gtex").get_tex_id(), "depth_gtex", true);
        show_gl_texture(m_view->m_ao_tex.get_tex_id(), "ao_tex", true);
        show_gl_texture(m_view->m_ao_blurred_tex.get_tex_id(), "ao_blurred_tex", true);
        // // show_gl_texture(m_core->m_view->m_rvec_tex.get_tex_id(), "rvec_tex", true);
        // show_gl_texture(m_view->m_depth_linear_tex.get_tex_id(), "depth_linear_tex", true);

        // show_gl_texture(m_core->m_lattice_cpu_test->m_input_tex.get_tex_id(), "lattice_input_tex");
        // show_gl_texture(m_core->m_lattice_cpu_test->m_output_tex.get_tex_id(), "lattice_output_tex");

        // show_gl_texture(m_core->m_ball_detector->m_input_tex.get_tex_id(), "input_rgba_tex");

        // show_gl_texture(m_core->m_cnn->m_input_tex.get_tex_id(), "input_tex");
        // show_gl_texture(m_core->m_cnn->m_output_tex.get_tex_id(), "output_tex");
        // show_gl_texture(m_core->m_cnn->m_gt_tex.get_tex_id(), "gt_tex");

        if(m_view->m_scene->does_mesh_with_name_exist("mesh_test")){
            if (auto mesh_gpu = m_view->m_scene->get_mesh_with_name("mesh_test")->m_mesh_gpu.lock()) {
                show_gl_texture(mesh_gpu->m_rgb_tex->get_tex_id(), "m_rgb_tex", true);
                show_gl_texture(mesh_gpu->m_thermal_colored_tex->get_tex_id(), "m_thermal_colored_tex", true);

                if(ImGui::Button("Save rgb tex") ){
                    cv::Mat rgb_mat;
                    rgb_mat=mesh_gpu->m_rgb_tex->download_to_cv_mat();

                    // //debg why its all alpha 0
                    // for(int y=0; y<rgb_mat.rows; y++){
                    //     for(int x=0; x<rgb_mat.cols; x++){
                    //         cv::Vec4f pix;
                    //         pix=rgb_mat.at<cv::Vec4f>(y,x);
                    //         VLOG(1) << "pix is " << pix[0] << " " <<pix[1] << " " << pix[2] << " " << pix[3];
                    //     }
                    // }

                    cv::Mat rgb_mat_bgr;
                    cv::cvtColor(rgb_mat, rgb_mat_bgr, CV_BGRA2RGBA);

                    //convert to 8u
                    rgb_mat_bgr*=255;
                    cv::Mat rgb_mat_8;
                    rgb_mat_bgr.convertTo(rgb_mat_8, CV_8UC1);

                    //flip
                    cv::Mat rgb_mat_8_flipped;
                    cv::flip(rgb_mat_8, rgb_mat_8_flipped, 0);

                    cv::imwrite("./rgb_tex.png",rgb_mat_8_flipped);
                }
                if(ImGui::Button("Save thermal_colored tex") ){
                    cv::Mat thermal_colored_mat;
                    thermal_colored_mat=mesh_gpu->m_thermal_colored_tex->download_to_cv_mat( );

                    cv::Mat rgb_mat_bgr;
                    cv::cvtColor(thermal_colored_mat, rgb_mat_bgr, CV_BGRA2RGBA);

                    //convert to 8u
                    rgb_mat_bgr*=255;
                    cv::Mat rgb_mat_8;
                    rgb_mat_bgr.convertTo(rgb_mat_8, CV_8UC1);

                    //flip
                    cv::Mat rgb_mat_8_flipped;
                    cv::flip(rgb_mat_8, rgb_mat_8_flipped, 0);

                    cv::imwrite("./thermal_colored_tex.png",rgb_mat_8_flipped);
                }


            }
        }
    }
 

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Misc")) {
        ImGui::SliderInt("log_level", &loguru::g_stderr_verbosity, -3, 9);
    }


    ImGui::Separator();
    ImGui::Text(("Nr of points: " + format_with_commas(m_view->m_scene->get_total_nr_vertices())).data());
    ImGui::Text(("Nr of triangles: " + format_with_commas(m_view->m_scene->get_total_nr_vertices())).data());
    ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);


    // ImGui::InputText("exported filename", m_core->m_exported_filename, 1000);
    // if (ImGui::Button("Write PLY")){
    //     m_core->write_ply();
    // }
    // if (ImGui::Button("Write OBJ")){
    //     m_core->write_obj();
    // }

    // static float f = 0.0f;
    // ImGui::Text("Hello, world!");
    // ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    if (ImGui::Button("Test Window")) m_show_demo_window ^= 1;
    if (ImGui::Button("Profiler Window")) m_show_profiler_window ^= 1;
    if (ImGui::Button("Player Window")) m_show_player_window ^= 1;




    // if (ImGui::Curve("Das editor", ImVec2(400, 200), 10, foo))
    // {
    //   // foo[0].y=ImGui::CurveValue(foo[0].x, 5, foo);
    //   // foo[1].y=ImGui::CurveValue(foo[1].x, 5, foo);
    //   // foo[2].y=ImGui::CurveValue(foo[2].x, 5, foo);
    //   // foo[3].y=ImGui::CurveValue(foo[3].x, 5, foo);
    //   // foo[4].y=ImGui::CurveValue(foo[4].x, 5, foo);
    // }



    ImGui::End();


   if (m_show_profiler_window && Profiler_ns::m_timings.size()>0 ){
        ImGuiWindowFlags profiler_window_flags = 0;
        profiler_window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
        int nr_timings=Profiler_ns::m_timings.size();
        ImVec2 size(330*m_hidpi_scaling,50*m_hidpi_scaling*nr_timings);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowPos(ImVec2(canvas_size.x -size.x , 0));
        ImGui::Begin("Profiler", nullptr, profiler_window_flags);
        ImGui::PushItemWidth(100*m_hidpi_scaling);


        for (size_t i = 0; i < Profiler_ns::m_ordered_timers.size(); ++i){
            const std::string name = Profiler_ns::m_ordered_timers[i];
            auto stats=Profiler_ns::m_stats[name];
            auto times=Profiler_ns::m_timings[name];
       
            std::stringstream stream_exp_mean;
            std::stringstream stream_mean;
            // stream_cma << std::fixed <<  std::setprecision(1) << stats.exp_mean;
            stream_exp_mean << std::fixed <<  std::setprecision(1) << stats.exp_mean;
            stream_mean << std::fixed <<  std::setprecision(1) << stats.mean;
            // std::string s_cma = stream_cma.str();

    //    std::string title = times.first +  "\n" + "(" + s_exp + ")" + "(" + s_cma + ")";
            std::string title = name +  "\n" + "exp_avg: " + stream_exp_mean.str() + " ms " + "(avg:"+stream_mean.str()+")";
            // std::string title = name +  "\n" + "exp_avg: " + stream_exp_mean.str() + " ms " + "("+stream_mean.str()+")";
            ImGui::PlotLines(title.data(), times.data() , times.size() ,times.get_front_idx() );
        }
        ImGui::End();
    } 

    // if (m_player){
    //     ImGuiWindowFlags player_window_flags = 0;
    //     player_window_flags |=  ImGuiWindowFlags_NoTitleBar;
    //     ImVec2 size(135*m_hidpi_scaling,56*m_hidpi_scaling);
    //     ImGui::SetNextWindowSize(size);
    //     ImGui::SetNextWindowPos(ImVec2(canvas_size.x -size.x , canvas_size.y -size.y ));
    //     ImGui::Begin("Player", nullptr, player_window_flags);
    //     ImGui::PushItemWidth(135*m_hidpi_scaling);


    //     ImVec2 button_size(25*m_hidpi_scaling,25*m_hidpi_scaling);
    //     const char* icon_play = m_player->is_paused() ? ICON_FA_PLAY : ICON_FA_PAUSE;
    //     //play/pause button
    //     if(ImGui::Button(icon_play,button_size)){
    //         //if it's paused, then start it
    //         if (m_player->is_paused()){
    //             m_player->play();
    //             // m_core->m_player->m_player_should_continue_after_step =true;
    //             // m_core->m_player->m_player_should_do_one_step=true;
    //         }else{
    //             m_player->pause();
    //             // m_core->m_player->m_player_should_continue_after_step =false;
    //             // m_core->m_player->m_player_should_do_one_step=false;
    //         }
    //     }
    //     ImGui::SameLine();
    //     if(ImGui::Button(ICON_FA_STEP_FORWARD,button_size)){
    //         // m_core->m_player_should_do_one_step=true;
    //     }
    //     // ImGui::SameLine();
    //     // const char* icon_should_continue = m_core->m_player->m_player_should_continue_after_step? ICON_FA_STOP : ICON_FA_FAST_FORWARD;
    //     // if(ImGui::Button(icon_should_continue,button_size)){
    //     //     // //if it's paused, then start it
    //     //     // if (m_core->m_player->is_paused()){
    //     //     //     m_core->m_player->play();
    //     //     //     m_core->m_player->m_player_should_continue_after_step =true;
    //     //     //     m_core->m_player->m_player_should_do_one_step=true;
    //     //     // }else{
    //     //     //     m_core->m_player->pause();
    //     //     //     m_core->m_player->m_player_should_continue_after_step =false;
    //     //     //     m_core->m_player->m_player_should_do_one_step=false;
    //     //     // }
    //     //
    //     // }
    //     ImGui::SameLine();
    //     if(ImGui::Button(ICON_FA_UNDO,button_size)){
    //         m_player->reset();
    //     }

    //     ImGui::End();
    // }




    // 3. Show the ImGui test window. Most of the sample code is in ImGui::ShowTestWindow()
    if (m_show_demo_window) {
        ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
        ImGui::ShowDemoWindow(&m_show_demo_window);
    }


}

// void Gui::show(const cv::Mat& cv_mat, const std::string name){

//     if(!cv_mat.data){
//         VLOG(3) << "Showing empty image, discaring";
//         return;
//     }

//     std::lock_guard<std::mutex> lock(m_add_cv_mats_mutex);  // so that "show" can be usef from any thread

//     m_cv_mats_map[name] = cv_mat.clone(); //TODO we shouldnt clone on top of this one because it might be at the moment used for transfering between cpu and gpu
//     m_cv_mats_dirty_map[name]=true;

// }

void Gui::show_images(){

    //TODO check if the cv mats actually changed, maybe a is_dirty flag
    for (auto const& x : m_cv_mats_map){
        std::string name=x.first;

        //check if it's dirty, if the cv mat changed since last time we displayed it
        if(m_cv_mats_dirty_map[name]){
            m_cv_mats_dirty_map[name]=false;
            //check if there is already a texture with the same name 
            auto got= m_textures_map.find(name);
            if(got==m_textures_map.end() ){
                //the first time we shot this texture so we add it to the map otherwise there is already a texture there so we just update it
                m_textures_map.emplace(name, (name) ); //using inplace constructor of the Texture2D so we don't use a move constructor or something similar
            }
            //upload to this texture, either newly created or not
            gl::Texture2D& tex= m_textures_map[name];
            tex.upload_from_cv_mat(x.second);
        }


        gl::Texture2D& tex= m_textures_map[name];
        show_gl_texture(tex.get_tex_id(), name);


    }
}



void Gui::show_gl_texture(const int tex_id, const std::string window_name, const bool flip){
    //show camera left
    if(tex_id==-1){
        return;
    }
    ImGuiWindowFlags window_flags = 0;
    ImGui::Begin(window_name.c_str(), nullptr, window_flags);
    if(flip){ //framebuffer in gpu are stored upside down  for some reson
        ImGui::Image((ImTextureID)(uintptr_t)tex_id, ImGui::GetContentRegionAvail(), ImVec2(0,1), ImVec2(1,0) );  //the double cast is needed to avoid compiler warning for casting int to void  https://stackoverflow.com/a/30106751
    }else{
        ImGui::Image((ImTextureID)(uintptr_t)tex_id, ImGui::GetContentRegionAvail()); 
    }
    ImGui::End();
}

//similar to libigl draw_text https://github.com/libigl/libigl/blob/master/include/igl/opengl/glfw/imgui/ImGuiMenu.cpp
void Gui::draw_overlays(){

    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    bool visible = true;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("ViewerLabels", &visible,
        ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoInputs);

    for (int i = 0; i < (int)m_view->m_meshes_gl.size(); i++) {
        MeshSharedPtr mesh= m_view->m_meshes_gl[i]->m_core;
        //draw vert ids
        if(mesh->m_vis.m_is_visible && mesh->m_vis.m_show_vert_ids){
            for (int i = 0; i < mesh->V.rows(); ++i){
                draw_overlay_text( mesh->V.row(i), mesh->m_model_matrix.cast<float>().matrix(), std::to_string(i), mesh->m_vis.m_label_color );
            }
        }
        //draw vert coords in x,y,z format
        if(mesh->m_vis.m_is_visible && mesh->m_vis.m_show_vert_coords){
            for (int i = 0; i < mesh->V.rows(); ++i){
                std::string coord_string = "(" + std::to_string(mesh->V(i,0)) + "," + std::to_string(mesh->V(i,1)) + "," + std::to_string(mesh->V(i,2)) + ")";
                draw_overlay_text( mesh->V.row(i), mesh->m_model_matrix.cast<float>().matrix(), coord_string, mesh->m_vis.m_label_color );
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();


}

//similar to libigl draw_text https://github.com/libigl/libigl/blob/master/include/igl/opengl/glfw/imgui/ImGuiMenu.cpp
void Gui::draw_overlay_text(const Eigen::Vector3d pos, const Eigen::Matrix4f model_matrix, const std::string text, const Eigen::Vector3f color){
    // std::cout <<"what" << std::endl;
    Eigen::Vector4f pos_4f;
    pos_4f << pos.cast<float>(), 1.0;

    Eigen::Matrix4f M = model_matrix;
    Eigen::Matrix4f V = m_view->m_camera->view_matrix();
    Eigen::Matrix4f P = m_view->m_camera->proj_matrix(m_view->m_viewport_size);
    Eigen::Matrix4f MVP = P*V*M;

    pos_4f= MVP * pos_4f;

    pos_4f = pos_4f.array() / pos_4f(3);
    pos_4f = pos_4f.array() * 0.5f + 0.5f;
    pos_4f(0) = pos_4f(0) * m_view->m_viewport_size(0);
    pos_4f(1) = pos_4f(1) * m_view->m_viewport_size(1);

    Eigen::Vector3f pos_screen= pos_4f.head(3);

    // Draw text labels slightly bigger than normal text
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2,
        // ImVec2(pos_screen[0] , ( - pos_screen[1])),
        ImVec2(pos_screen[0]*m_hidpi_scaling , (m_view->m_viewport_size(1) - pos_screen[1]) *m_hidpi_scaling ),
        ImGui::GetColorU32(ImVec4(
            color(0),
            color(1),
            color(2),
            1.0)),
    &text[0], &text[0] + text.size());
}

void Gui::edit_transform(const MeshSharedPtr& mesh){


    Eigen::Matrix4f widget_placement;
    widget_placement.setIdentity();
    Eigen::Matrix4f view = m_view->m_camera->view_matrix();
    Eigen::Matrix4f proj = m_view->m_camera->proj_matrix(m_view->m_viewport_size);

    ImGuizmo::BeginFrame();
    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    Eigen::Matrix4f delta;
    delta.setIdentity();
    
    ImGuizmo::Manipulate(view.data(), proj.data(), m_guizmo_operation, m_guizmo_mode, mesh->m_model_matrix.cast<float>().data(), delta.data() );
    if(m_guizmo_operation==ImGuizmo::SCALE){
        delta=Eigen::Matrix4f::Identity()-(Eigen::Matrix4f::Identity()-delta)*0.1; //scaling is for some reason very fast, make it a bit slower
    }
    


    //for doubles
    Eigen::Matrix4f new_model_matrix;
    new_model_matrix=mesh->m_model_matrix.matrix().cast<float>();
    new_model_matrix=delta*mesh->m_model_matrix.cast<float>().matrix();    
    mesh->m_model_matrix=Eigen::Affine3d(new_model_matrix.cast<double>());

}

float Gui::pixel_ratio(){
    // Computes pixel ratio for hidpi devices
    int buf_size[2];
    int win_size[2];
    GLFWwindow* window = glfwGetCurrentContext();
    glfwGetFramebufferSize(window, &buf_size[0], &buf_size[1]);
    glfwGetWindowSize(window, &win_size[0], &win_size[1]);
    return (float) buf_size[0] / (float) win_size[0];
}

float Gui::hidpi_scaling(){
    // Computes scaling factor for hidpi devices
    //float xscale, yscale;
    //GLFWwindow* window = glfwGetCurrentContext();
    //glfwGetWindowContentScale(window, &xscale, &yscale);
    //return 0.5 * (xscale + yscale);
    return 1.0;
}


// void Gui::register_module(const std::shared_ptr<RosBagPlayer> player){
//     m_player=player;
// }

// void Gui::register_module(const std::shared_ptr<Texturer> texturer){
//     m_texturer=texturer;
// }

// void Gui::register_module(const std::shared_ptr<FireDetector> fire_detector){
//     m_fire_detector=fire_detector;
// }

// void Gui::register_module(const std::shared_ptr<Mesher> mesher){
//     m_mesher=mesher;
// }

// void Gui::register_module(const std::shared_ptr<LatticeCPU_test> latticeCPU_test){
//     m_latticeCPU_test=latticeCPU_test;
// }

// void Gui::register_module(const std::shared_ptr<BallDetector> ball_detector){
//     m_ball_detector=ball_detector;
// }



void Gui::init_style() {
    //based on https://www.unknowncheats.me/forum/direct3d/189635-imgui-style-settings.html
    ImGuiStyle *style = &ImGui::GetStyle();

    style->WindowPadding = ImVec2(15, 15);
    style->WindowRounding = 0.0f;
    style->FramePadding = ImVec2(5, 5);
    style->FrameRounding = 4.0f;
    style->ItemSpacing = ImVec2(12, 8);
    style->ItemInnerSpacing = ImVec2(8, 6);
    style->IndentSpacing = 25.0f;
    style->ScrollbarSize = 8.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize = 5.0f;
    style->GrabRounding = 3.0f;

    style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
    style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.05f, 0.07f, 0.85f);
    style->Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.80f, 0.83f, 0.0f);
    style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
    style->Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    // style->Colors[ImGuiCol_ComboBg] = ImVec4(0.19f, 0.18f, 0.21f, 1.00f);
    style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 0.35f);
    style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    // style->Colors[ImGuiCol_Column] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    // style->Colors[ImGuiCol_ColumnHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    // style->Colors[ImGuiCol_ColumnActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    // style->Colors[ImGuiCol_CloseButton] = ImVec4(0.40f, 0.39f, 0.38f, 0.16f);
    // style->Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.40f, 0.39f, 0.38f, 0.39f);
    // style->Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
    style->Colors[ImGuiCol_PlotLines] = ImVec4(0.63f, 0.6f, 0.6f, 0.94f);
    style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.63f, 0.6f, 0.6f, 0.94f);
    style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
    style->Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);
}
