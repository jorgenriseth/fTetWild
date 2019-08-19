#include "MeshIO.hpp"

#include <floattetwild/Logger.hpp>
#include <floattetwild/MshSaver.h>
#include <floattetwild/MeshImprovement.h>
#include <floattetwild/Statistics.h>

#include <igl/Timer.h>

#include <igl/write_triangle_mesh.h>
#include <igl/boundary_facets.h>
#include <igl/remove_unreferenced.h>


#include <geogram/mesh/mesh_io.h>
#include <geogram/mesh/mesh_repair.h>
#include <geogram/numerics/predicates.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_reorder.h>


#include <numeric>


namespace floatTetWild
{
	namespace
	{
		void extract_surface_mesh(const Mesh &mesh, const std::function<bool(int)> &skip_tet, const std::function<bool(int)> &skip_vertex, Eigen::Matrix<Scalar, Eigen::Dynamic, 3> &VS, Eigen::Matrix<int, Eigen::Dynamic, 3> &FS)
		{
			const auto &points = mesh.tet_vertices;
			const auto &tets = mesh.tets;

			Eigen::Matrix<Scalar, Eigen::Dynamic, 3> points_tmp(points.size(), 3);
			Eigen::Matrix<int, Eigen::Dynamic, 4> tets_tmp(tets.size(), 4);

			std::map<int, int> old_2_new;
			int index = 0;


			for(size_t i = 0; i < points.size(); ++i){
				if(skip_vertex(i))
					continue;
				old_2_new[i] = index;
				points_tmp.row(index) = points[i].pos;
				++index;
			}

			points_tmp.conservativeResize(index, 3);

			index = 0;
			for(size_t i = 0; i < tets.size(); ++i){
				if (skip_tet(i))
					continue;
				for (int j = 0; j < 4; j++) {
					tets_tmp(index, j) = old_2_new[tets[i][j]];
				}
				++index;
			}
			tets_tmp.conservativeResize(index, 4);


			Eigen::VectorXi I;
			igl::boundary_facets(tets_tmp, FS);
			igl::remove_unreferenced(points_tmp, FS, VS, FS, I);
			// for(int i=0;i < FS.rows();i++){
			// 	int tmp = FS(i, 0);
			// 	FS(i, 0) = FS(i, 2);
			// 	FS(i, 2) = tmp;
			// }
		}

		void write_mesh_aux(const std::string &path, const Mesh &mesh, const std::vector<int> &t_ids, const std::vector<Scalar> &color, const bool binary, const std::function<bool(int)> &skip_tet, const std::function<bool(int)> &skip_vertex)
		{
			std::string output_format = path.substr(path.size() - 4, 4);

			if (output_format == "mesh") {
				if(binary)
					logger().warn("Only non binary mesh supported, ignoring");

				std::ofstream f(path);
				f.precision(std::numeric_limits<Scalar>::digits10 + 1);

				f << "MeshVersionFormatted 1" << std::endl;
				f << "Dimension 3" << std::endl;

				int cnt_v = 0;
				std::map<int, int> old_2_new;
				for (int i = 0; i < mesh.tet_vertices.size(); i++) {
					if (!skip_vertex(i)) {
						old_2_new[i] = cnt_v;
						cnt_v++;
					}
				}
				int cnt_t = 0;
				for (const int i : t_ids) {
					if (!skip_tet(i))
						cnt_t++;
				}


				f << "Vertices" << std::endl << cnt_v << std::endl;

				for (size_t i = 0; i < mesh.tet_vertices.size(); i++){
					if(skip_vertex(i))
						continue;
					f << mesh.tet_vertices[i][0] << " " << mesh.tet_vertices[i][1] << " " << mesh.tet_vertices[i][2] << " " << 0 << std::endl;
				}

				f << "Triangles" << std::endl << 0 <<std::endl;
				f << "Tetrahedra" << std::endl << cnt_t << std::endl;

				for (const int i : t_ids) {
					if(skip_tet(i))
						continue;
					for (int j = 0; j < 4; j++) {
						f << old_2_new[mesh.tets[i][j]] + 1 << " ";
					}
					f << 0 << std::endl;
				}

				f << "End";
				f.close();
			}
			else {
				assert(color.empty() || color.size() == mesh.tet_vertices.size() || color.size() == mesh.tets.size());

				PyMesh::MshSaver mesh_saver(path, binary);

				std::map<int, int> old_2_new;
				int cnt_v = 0;
				for (int i = 0; i < mesh.tet_vertices.size(); i++) {
					if (!skip_vertex(i)) {
						old_2_new[i] = cnt_v;
						cnt_v++;
					}
				}
				int cnt_t = 0;
				for (const int i : t_ids) {
					if (!skip_tet(i))
						cnt_t++;
				}
				PyMesh::VectorF V_flat(cnt_v * 3);
				PyMesh::VectorI T_flat(cnt_t * 4);

				size_t index = 0;
				for (size_t i = 0; i < mesh.tet_vertices.size(); ++i) {
					if (skip_vertex(i))
						continue;
					for (int j = 0; j < 3; j++)
						V_flat[index * 3 + j] = mesh.tet_vertices[i][j];
					index++;
				}

				index = 0;
				for (const int i : t_ids) {
					if (skip_tet(i))
						continue;
					T_flat[index * 4 + 0] = old_2_new[mesh.tets[i][0]];
					T_flat[index * 4 + 1] = old_2_new[mesh.tets[i][1]];
					T_flat[index * 4 + 2] = old_2_new[mesh.tets[i][3]];
					T_flat[index * 4 + 3] = old_2_new[mesh.tets[i][2]];
					index++;
				}

				mesh_saver.save_mesh(V_flat, T_flat, 3, mesh_saver.TET);

				if(color.size() == mesh.tets.size())
				{
					PyMesh::VectorF color_flat(cnt_t);
					index = 0;
					for (const int i : t_ids) {
						if (skip_tet(i))
							continue;
						color_flat[index++] = color[i];
					}
					mesh_saver.save_elem_scalar_field("color", color_flat);
				}
				else if(color.size() == mesh.tet_vertices.size())
				{
					PyMesh::VectorF color_flat(cnt_v);
					index = 0;
					for (int i = 0; i < mesh.tet_vertices.size(); i++) {
						if (skip_vertex(i))
							continue;
						color_flat[index++] = color[i];
					}
					mesh_saver.save_scalar_field("color", color_flat);
				}
			}
		}
	}

	bool MeshIO::load_mesh(const std::string &path, std::vector<Vector3> &points, std::vector<Vector3i> &faces, GEO::Mesh& input, std::vector<int> &flags)
	{
		logger().debug("Loading mesh at {}...", path);
		igl::Timer timer; timer.start();

		input.clear(false,false);

		const bool ok = GEO::mesh_load(path, input);

		if(!ok)
			return false;

		bool is_valid = (flags.size() == input.facets.nb());
		if(is_valid)
		{
			assert(flags.size() == input.facets.nb());
			GEO::Attribute<int> bflags(input.facets.attributes(), "bbflags");
			for (int index = 0; index < (int) input.facets.nb(); ++index) {
				bflags[index] = flags[index];
			}
		}

		if(!input.facets.are_simplices()) {
			mesh_repair(
				input,
				GEO::MeshRepairMode(GEO::MESH_REPAIR_TRIANGULATE | GEO::MESH_REPAIR_QUIET)
				);
		}

		// #ifdef FLOAT_TETWILD_USE_FLOAT
		// 		input.vertices.set_single_precision();
		// #else
		// 		input.vertices.set_double_precision();
		// #endif

		GEO::mesh_reorder(input, GEO::MESH_ORDER_MORTON);

		if(is_valid)
		{
			flags.clear();
			flags.resize(input.facets.nb());
			GEO::Attribute<int> bflags(input.facets.attributes(), "bbflags");
			for (int index = 0; index < (int) input.facets.nb(); ++index) {
				flags[index] = bflags[index];
			}
		}

		points.resize(input.vertices.nb());
		for(size_t i=0; i<points.size(); i++)
			points[i]<<(input.vertices.point(i))[0], (input.vertices.point(i))[1], (input.vertices.point(i))[2];

		faces.resize(input.facets.nb());
		for(size_t i=0; i<faces.size(); i++)
			faces[i]<<input.facets.vertex(i, 0), input.facets.vertex(i, 1), input.facets.vertex(i, 2);


		return ok;
	}

	void MeshIO::write_mesh(const std::string &path, const Mesh &mesh, const std::vector<int> &t_ids, const bool only_interior, const bool binary) {
		logger().info("Writing mesh to {}...", path);
		igl::Timer timer; timer.start();

		if (only_interior) {
			const auto skip_tet = [&mesh](const int i){ return mesh.tets[i].is_outside; };
			const auto skip_vertex = [&mesh](const int i) { return mesh.tet_vertices[i].is_outside; };
			write_mesh_aux(path, mesh, t_ids, std::vector<Scalar>(), binary, skip_tet, skip_vertex);
		} else {
			timer.start();
			const auto skip_tet = [&mesh](const int i) { return mesh.tets[i].is_removed; };
			const auto skip_vertex = [&mesh](const int i) { return mesh.tet_vertices[i].is_removed; };
			write_mesh_aux(path, mesh, t_ids, std::vector<Scalar>(), binary, skip_tet, skip_vertex);
		}

		timer.stop();
		logger().info(" took {}s", timer.getElapsedTime());
	}

	void MeshIO::write_mesh(const std::string &path, const Mesh &mesh, const bool only_interior, const std::vector<Scalar> &color, const bool binary)
	{
		logger().info("Writing mesh to {}...", path);
		igl::Timer timer; timer.start();

		std::vector<int> t_ids(mesh.tets.size());
		std::iota (std::begin(t_ids), std::end(t_ids), 0); // Fill with 0, 1, ..., n.

		if(only_interior)
		{
			const auto skip_tet = [&mesh](const int i){ return mesh.tets[i].is_outside; };
			const auto skip_vertex = [&mesh](const int i) { return mesh.tet_vertices[i].is_outside; };
			write_mesh_aux(path, mesh, t_ids, color, binary, skip_tet, skip_vertex);
		}
		else
		{
			const auto skip_tet = [&mesh](const int i){ return mesh.tets[i].is_removed; };
			const auto skip_vertex = [&mesh](const int i) { return mesh.tet_vertices[i].is_removed; };
			write_mesh_aux(path, mesh, t_ids, color, binary, skip_tet, skip_vertex);
		}

		timer.stop();
		logger().info(" took {}s", timer.getElapsedTime());
	}


	void MeshIO::write_surface_mesh(const std::string &path, const Mesh &mesh, const bool only_interior)
	{
		logger().debug("Extracting and writing surface to {}...", path);
		igl::Timer timer; timer.start();


		Eigen::Matrix<Scalar, Eigen::Dynamic, 3> V_sf;
		Eigen::Matrix<int, Eigen::Dynamic, 3> F_sf;
		if(only_interior)
		{
			const auto skip_tet = [&mesh](const int i){ return mesh.tets[i].is_outside; };
			const auto skip_vertex = [&mesh](const int i) { return mesh.tet_vertices[i].is_outside; };
			extract_surface_mesh(mesh, skip_tet, skip_vertex, V_sf, F_sf);
		}
		else
		{
			const auto skip_tet = [&mesh](const int i){ return mesh.tets[i].is_removed; };
			const auto skip_vertex = [&mesh](const int i) { return mesh.tet_vertices[i].is_removed; };
			extract_surface_mesh(mesh, skip_tet, skip_vertex, V_sf, F_sf);
		}

		igl::write_triangle_mesh(path, V_sf, F_sf);

		timer.stop();
		logger().info(" took {}s", timer.getElapsedTime());
	}
}