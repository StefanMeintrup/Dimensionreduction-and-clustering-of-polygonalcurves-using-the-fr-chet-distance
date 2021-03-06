#pragma once

#include <unordered_map>

#include <boost/chrono/include.hpp>

#include "random.hpp"
#include "curve.hpp"
#include "frechet.hpp"

namespace Clustering {

class Centers : public std::vector<std::size_t> {
public:
	inline const auto get(const std::size_t i) const {
		return this->operator[](i);
	}
};

class Cluster_Assignment : public std::unordered_map<std::size_t, std::vector<std::size_t>> {
public:
	inline const auto count(const std::size_t i) const {
		return this->operator[](i).size();
	}
	
	inline const auto get(const std::size_t i, const std::size_t j) const {
		return this->operator[](i)[j];
	}
	
};

struct Clustering_Result {
	Centers centers;
	distance_t value;
	double running_time;
	Cluster_Assignment assignment;
	
	inline auto get(const std::size_t i) const {
		return centers.get(i);
	}
	inline auto size() const {
		return centers.size();
	}
};


inline void cheap_dist(const std::size_t i, const std::size_t j, const Curves &in, std::vector<std::vector<distance_t>> &distances) {
	if (distances[i][j] < 0) {
		const auto lb = std::sqrt(std::max(in[i].front().dist_sqr(in[j].front()), in[i].back().dist_sqr(in[j].back())));
		const auto ub = Frechet::Discrete::distance(in[i], in[j]);
		const auto dist = Frechet::Continuous::distance_cuda(in[i], in[j], ub.value, lb, 0.001, false);
		distances[j][i] = dist.value;
		distances[i][j] = dist.value;
	}
}

inline std::size_t getNearestCenter(const std::size_t i, const Curves &in, const Centers &centers, 
		std::vector<std::vector<distance_t>> &distances) {
	const auto infty = std::numeric_limits<distance_t>::infinity();
	// cost for curve is infinity
	auto min_cost_elem = infty;
	std::size_t nearest = 0;
	
	// except there is a center with smaller cost, then choose the one with smallest cost
	for (std::size_t j = 0; j < centers.size(); ++j) {
		cheap_dist(i, centers[j], in, distances);
		if (distances[i][centers[j]] < min_cost_elem) {
			min_cost_elem = distances[i][centers[j]];
			nearest = j;
		}
	}
	return nearest;
}

inline auto curve_cost(const std::size_t i, const Curves &in, const Centers &centers, std::vector<std::vector<distance_t>> &distances) {
	const auto infty = std::numeric_limits<distance_t>::infinity();
	// cost for curve is infinity
	auto min_cost_elem = infty;
	
	// except there is a center with smaller cost, then choose the one with smallest cost
	for (std::size_t j = 0; j < centers.size(); ++j) {
		cheap_dist(i, centers[j], in, distances);
		if (distances[i][centers[j]] < min_cost_elem) {
			min_cost_elem = distances[i][centers[j]];
		}
	}
	
	return min_cost_elem;
}

inline auto center_cost_sum(const Curves &in, const Centers &centers, std::vector<std::vector<distance_t>> &distances) {
	double cost = 0.0;
	
	// for all curves
	for (std::size_t i = 0; i < in.size(); ++i) {
		const auto min_cost_elem = curve_cost(i, in, centers, distances);
		cost += min_cost_elem;
	}
	return cost;
}

inline Cluster_Assignment getClusterAssignment(const Curves &in, const Centers &centers, std::vector<std::vector<distance_t>> &distances) {
	Cluster_Assignment result;
	const auto k = centers.size();
	
	if (k == 0) return result;
	
	for (std::size_t i = 0; i < k; ++i) result.emplace(i, std::vector<std::size_t>());
	
	for (std::size_t i = 0; i < in.size(); ++i) result[getNearestCenter(i, in, centers, distances)].push_back(i);
	
	return result;	
}

Clustering_Result gonzalez(const std::size_t num_centers, const Curves &in, const bool arya = false, const bool with_assignment = false) {
	const auto start = boost::chrono::process_real_cpu_clock::now();
	Clustering_Result result;
	
	if (in.empty()) return result;
		
	Centers centers;
	
	const auto n = in.size();
	
	centers.push_back(0);
	
	distance_t curr_maxdist = 0;
	std::size_t curr_maxcurve = 0;
	
	std::vector<std::vector<distance_t>> distances(in.size(), std::vector<distance_t>(in.size(), -1.0));
	
	// no cost for distances from curves to themselves
	for (std::size_t i = 0; i < in.size(); ++i) distances[i][i] = 0;
	
	{
		// remaining centers
		for (std::size_t i = 1; i < num_centers; ++i) {
			
			curr_maxdist = 0;
			curr_maxcurve = 0;
			{
			
				// all curves
				for (std::size_t j = 0; j < in.size(); ++j) {
					
					auto curr_curve_cost = curve_cost(j, in, centers, distances);
					
					if (curr_curve_cost > curr_maxdist) {
						curr_maxdist = curr_curve_cost;
						curr_maxcurve = j;
					}
					
				}
				#if DEBUG
				std::cout << "found center no. " << i+1 << std::endl;
				#endif
				
				centers.push_back(curr_maxcurve);
			}	
		}
	}
	
	if (arya) {
		
		auto cost = center_cost_sum(in, centers, distances);
		auto approxcost = cost;
		auto gamma = 1/(3 * num_centers * in.size());
		auto found = false;
		
		// try to improve current solution
		while (true) {
			found = false;
			
			// go through all centers
			for (std::size_t i = 0; i < num_centers; ++i) {
				auto curr_centers = centers;
				
				// check if there is a better center among all other curves
				for (std::size_t j = 0; j < in.size(); ++j) {
					// continue if curve is already part of center set
					if (std::find(curr_centers.begin(), curr_centers.end(), j) != curr_centers.end()) continue;
					
					// swap
					curr_centers[i] = j;
					// new cost
					auto curr_cost = center_cost_sum(in, curr_centers, distances);
					// check if improvement is done
					if (cost - gamma * approxcost > curr_cost) {
						cost = curr_cost;
						centers = curr_centers;
						found = true;
					}
				}
			}
			if (not found) break;
		}
		curr_maxdist = cost;
	}
	
	if (with_assignment) {
		result.assignment = getClusterAssignment(in, centers, distances);
	}
	
	auto end = boost::chrono::process_real_cpu_clock::now();
	result.centers = centers;
	result.value = curr_maxdist;
	result.running_time = (end-start).count() / 1000000000.0;
	return result;
}

Clustering_Result arya(const std::size_t num_centers, const Curves &in) {
	return gonzalez(num_centers, in, true);
}

Clustering_Result one_median_approx(const double epsilon, const Curves &in) {
	const auto start = boost::chrono::process_real_cpu_clock::now();
	Clustering_Result result;
	Centers centers;
	
	const auto n = in.size();
	
	const auto s = std::ceil(60);
	const auto t = std::ceil(std::log(60)/(epsilon*epsilon));
	
	Uniform_Random_Generator<double> ugen;
	
	const auto candidates = ugen.get(s);
	const auto witnesses = ugen.get(t);
	
	std::vector<std::vector<distance_t>> distances(in.size(), 
		std::vector<distance_t>(in.size(), -1.0));
	
	std::size_t best_candidate = 0;
	distance_t best_objective_value = std::numeric_limits<distance_t>::infinity();
	
	for (std::size_t i = 0; i < candidates.size(); ++i) {
		
		const std::size_t candidate = std::floor(candidates[i] * n);
		double objective = 0;
		
		for (std::size_t j = 0; j < witnesses.size(); ++j) {
			const std::size_t witness = std::floor(witnesses[j] * n);
			
			cheap_dist(candidate, witness, in, distances);
			objective += distances[candidate][witness];
		}
		
		if (objective < best_objective_value) {
			best_candidate = candidate;
			best_objective_value = objective;
		}
	}
	centers.push_back(best_candidate);
	auto end = boost::chrono::process_real_cpu_clock::now();
	result.centers = centers;
	result.value = center_cost_sum(in, centers, distances);
	result.running_time = (end-start).count() / 1000000000.0;
	return result;
}

Clustering_Result one_median_exhaustive(const Curves &in) {
	const auto start = boost::chrono::process_real_cpu_clock::now();
	Clustering_Result result;
	Centers centers;
	
	const auto n = in.size();
		
	std::vector<std::vector<distance_t>> distances(in.size(), 
		std::vector<distance_t>(in.size(), -1.0));
	
	std::size_t best_candidate = 0;
	distance_t best_objective_value = std::numeric_limits<distance_t>::infinity();
	
	for (std::size_t i = 0; i < in.size(); ++i) {
		
		double objective = 0;
		
		for (std::size_t j = 0; j < in.size(); ++j) {
			cheap_dist(i, j, in, distances);
			objective += distances[i][j];
		}
		
		if (objective < best_objective_value) {
			best_candidate = i;
			best_objective_value = objective;
		}
	}
	centers.push_back(best_candidate);
	auto end = boost::chrono::process_real_cpu_clock::now();
	result.centers = centers;
	result.value = best_objective_value;
	result.running_time = (end-start).count() / 1000000000.0;
	return result;
}

}
