/* -------------------------------------------------------------------------*
 *								SPHinXsys									*
 * -------------------------------------------------------------------------*
 * SPHinXsys (pronunciation: s'finksis) is an acronym from Smoothed Particle*
 * Hydrodynamics for industrial compleX systems. It provides C++ APIs for	*
 * physical accurate simulation and aims to model coupled industrial dynamic*
 * systems including fluid, solid, multi-body dynamics and beyond with SPH	*
 * (smoothed particle hydrodynamics), a meshless computational method using	*
 * particle discretization.													*
 *																			*
 * SPHinXsys is partially funded by German Research Foundation				*
 * (Deutsche Forschungsgemeinschaft) DFG HU1527/6-1, HU1527/10-1,			*
 *  HU1527/12-1 and HU1527/12-4													*
 *                                                                          *
 * Portions copyright (c) 2017-2022 Technical University of Munich and		*
 * the authors' affiliations.												*
 *                                                                          *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may  *
 * not use this file except in compliance with the License. You may obtain a*
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.       *
 *                                                                          *
 * ------------------------------------------------------------------------*/
/**
 * @file 	particle_dynamics_algorithms.h
 * @brief 	This is the classes for algorithms particle dynamics .
 * @detail	Generally, there are two types of particle dynamics algorithms.
 *			One leads to the change of particle states, the other not.
 *			There are 5 classes the first type. They are:
 * 			SimpleDynamics is without particle interaction. Particles just update their states;
 *			InteractionDynamics is with particle interaction with its neighbors;
 *			InteractionSplit is InteractionDynamics but using spliting algorithm;
 *			InteractionWithUpdate is with particle interaction with its neighbors and then update their states;
 *			Dynamics1Level is the most complex dynamics, has successive three steps: initialization, interaction and update.
 *			In order to avoid misusing of the above algorithms, type traits are used to make sure that the matching between
 *			the algorithm and local dynamics. For example, the LocalDynamics which matches InteractionDynamics must have
 *			the function interaction() but should not have the function update() or initialize().
 *			The existence of the latter suggests that more complex algorithms,
 *			such as InteractionWithUpdate or Dynamics1Level should be used.
 *			There are 2 classes for the second type.
 *			ReduceDynamics carries out a reduce operation through the particles.
 *			ReduceAverage further computes average of a ReduceDynamics for summation.
 *			Each particle dynamics is templated with a LocalDynamics and a DynamicsRange.
 *			The local dynamics defines the behavior of a single particle or with its neighbors,
 *			and is recognized by particle dynamics with the signature functions, like update, initialization and interaction.
 *			DynamicsRange define and range of particles for the dynamics.
 *			The default range is the entire body. Other ranges are BodyPartByParticle and BodyPartByCell.
 * @author	Chi Zhang, Fabien Pean and Xiangyu Hu
 */

#ifndef PARTICLE_DYNAMICS_ALGORITHMS_H
#define PARTICLE_DYNAMICS_ALGORITHMS_H

#include "particle_iterators.h"
#include "base_local_dynamics.h"
#include "base_particle_dynamics.hpp"

#include <type_traits>

namespace SPH
{
	template <class T, class = void>
	struct has_initialize : std::false_type
	{
	};

	template <class T>
	struct has_initialize<T, std::void_t<decltype(&T::initialize)>> : std::true_type
	{
	};

	template <class T, class = void>
	struct has_interaction : std::false_type
	{
	};

	template <class T>
	struct has_interaction<T, std::void_t<decltype(&T::interaction)>> : std::true_type
	{
	};

	template <class T, class = void>
	struct has_update : std::false_type
	{
	};

	template <class T>
	struct has_update<T, std::void_t<decltype(&T::update)>> : std::true_type
	{
	};

	/**
	 * @class SimpleDynamics
	 * @brief Simple particle dynamics without considering particle interaction
	 */
	template <class LocalDynamicsType>
	class SimpleDynamics : public LocalDynamicsType, public BaseDynamics<void>
	{
	public:
		template <class DynamicsIdentifier, typename... Args>
		SimpleDynamics(DynamicsIdentifier &identifier, Args &&...args)
			: LocalDynamicsType(identifier, std::forward<Args>(args)...),
			  BaseDynamics<void>(identifier.getSPHBody())
		{
			static_assert(!has_initialize<LocalDynamicsType>::value &&
							  !has_interaction<LocalDynamicsType>::value,
						  "LocalDynamicsType does not fulfill SimpleDynamics requirements");
		};
		virtual ~SimpleDynamics(){};
		/** The sequential function for executing the operations on particles. */
		virtual void exec(Real dt = 0.0) override
		{
			this->setUpdated();
			this->setupDynamics(dt);
			particle_for(this->identifier_.LoopRange(),
						 [&](size_t i)
						 { this->update(i, dt); });
		};
		/** The parallel function for executing the operations on particles. */
		virtual void parallel_exec(Real dt = 0.0) override
		{
			this->setUpdated();
			this->setupDynamics(dt);
			particle_parallel_for(this->identifier_.LoopRange(),
								  [&](size_t i)
								  { this->update(i, dt); });
		};
	};

	/**
	 * @class ReduceDynamics
	 * @brief Template class for particle-wise reduce operation, summation, max or min.
	 */
	template <class LocalDynamicsType>
	class ReduceDynamics : public LocalDynamicsType,
						   public BaseDynamics<typename LocalDynamicsType::ReduceReturnType>

	{
		using ReturnType = typename LocalDynamicsType::ReduceReturnType;

	public:
		template <class DynamicsIdentifier, typename... Args>
		ReduceDynamics(DynamicsIdentifier &identifier, Args &&...args)
			: LocalDynamicsType(identifier, std::forward<Args>(args)...),
			  BaseDynamics<ReturnType>(identifier.getSPHBody()){};
		virtual ~ReduceDynamics(){};

		using ReduceReturnType = ReturnType;
		std::string QuantityName() { return this->quantity_name_; };
		std::string DynamicsIdentifierName() { return this->identifier_.getName(); };
		/** The sequential function for executing the reduce operations on particles. */
		virtual ReturnType exec(Real dt = 0.0) override
		{
			this->setupDynamics(dt);
			ReturnType temp = particle_reduce(
				this->identifier_.LoopRange(), this->Reference(), this->getOperation(),
				[&](size_t i) -> ReturnType
				{ return this->reduce(i, dt); });
			return this->outputResult(temp);
		};
		/** The parallel function for executing the reduce operations on particles. */
		virtual ReturnType parallel_exec(Real dt = 0.0) override
		{
			this->setupDynamics(dt);
			ReturnType temp = particle_parallel_reduce(
				this->identifier_.LoopRange(), this->Reference(), this->getOperation(),
				[&](size_t i) -> ReturnType
				{ return this->reduce(i, dt); });
			return this->outputResult(temp);
		};
	};

	/**
	 * @class ReduceAverage
	 * @brief Template class for computing particle-wise averages
	 */
	template <class LocalDynamicsType>
	class ReduceAverage : public ReduceDynamics<LocalDynamicsType>
	{
		using ReturnType = typename LocalDynamicsType::ReduceReturnType;
		ReturnType outputAverage(ReturnType sum, size_t size_of_loop_range)
		{
			return sum / Real(size_of_loop_range);
		}

	public:
		template <class DynamicsIdentifier, typename... Args>
		ReduceAverage(DynamicsIdentifier &identifier, Args &&...args)
			: ReduceDynamics<LocalDynamicsType>(identifier, std::forward<Args>(args)...){};
		virtual ~ReduceAverage(){};
		/** The sequential function for executing the average operations on particles. */
		virtual ReturnType exec(Real dt = 0.0) override
		{
			ReturnType sum = ReduceDynamics<LocalDynamicsType>::exec(dt);
			return outputAverage(sum, this->identifier_.SizeOfLoopRange());
		};
		/** The parallel function for executing the average operations on particles. */
		virtual ReturnType parallel_exec(Real dt = 0.0) override
		{
			ReturnType sum = ReduceDynamics<LocalDynamicsType>::parallel_exec(dt);
			return outputAverage(sum, this->identifier_.SizeOfLoopRange());
		};
	};

	/**
	 * @class BaseInteractionDynamics
	 * @brief This is the base class for particle interaction with other particles
	 */
	template <class LocalDynamicsType>
	class BaseInteractionDynamics : public LocalDynamicsType, public BaseDynamics<void>
	{
	public:
		template <class BodyRelationType, typename... Args>
		BaseInteractionDynamics(BodyRelationType &body_relation, Args &&...args)
			: LocalDynamicsType(body_relation, std::forward<Args>(args)...),
			  BaseDynamics<void>(body_relation.getSPHBody()){};
		virtual ~BaseInteractionDynamics(){};

		/** pre process such as update ghost state */
		StdVec<BaseDynamics<void> *> pre_processes_;
		/** post process such as impose constraint */
		StdVec<BaseDynamics<void> *> post_processes_;
		/** sequential run the main interaction step between particles. */
		virtual void runMainStep(Real dt) = 0;
		/** parallel run the main interaction step between particles. */
		virtual void parallel_runMainStep(Real dt) = 0;
		/** sequential run the interactions between particles. */
		virtual void runInteraction(Real dt)
		{
			for (size_t k = 0; k < this->pre_processes_.size(); ++k)
				this->pre_processes_[k]->exec(dt);

			runMainStep(dt);

			for (size_t k = 0; k < this->post_processes_.size(); ++k)
				this->post_processes_[k]->exec(dt);
		};
		/** parallel run the interactions between particles. */
		virtual void parallel_runInteraction(Real dt)
		{
			for (size_t k = 0; k < this->pre_processes_.size(); ++k)
				this->pre_processes_[k]->parallel_exec(dt);

			parallel_runMainStep(dt);

			for (size_t k = 0; k < this->post_processes_.size(); ++k)
				this->post_processes_[k]->parallel_exec(dt);
		};

		virtual void exec(Real dt = 0.0) override
		{
			this->setUpdated();
			this->setupDynamics(dt);
			runInteraction(dt);
		};

		virtual void parallel_exec(Real dt = 0.0) override
		{
			this->setUpdated();
			this->setupDynamics(dt);
			parallel_runInteraction(dt);
		};
	};

	/**
	 * @class InteractionSplit
	 * @brief This is for the splitting algorithm
	 */
	template <class LocalDynamicsType>
	class InteractionSplit : public BaseInteractionDynamics<LocalDynamicsType>
	{
	protected:
		RealBody &real_body_;
		SplitCellLists &split_cell_lists_;

	public:
		template <class BodyRelationType, typename... Args>
		InteractionSplit(BodyRelationType &body_relation, Args &&...args)
			: BaseInteractionDynamics<LocalDynamicsType>(body_relation, std::forward<Args>(args)...),
			  real_body_(DynamicCast<RealBody>(this, body_relation.getSPHBody())),
			  split_cell_lists_(real_body_.getSplitCellLists())
		{
			real_body_.setUseSplitCellLists();
			static_assert(!has_initialize<LocalDynamicsType>::value &&
							  !has_update<LocalDynamicsType>::value,
						  "LocalDynamicsType does not fulfill InteractionSplit requirements");
		};
		virtual ~InteractionSplit(){};
		/** sequential run the main interaction step between particles. */
		virtual void runMainStep(Real dt) override
		{
			particle_for_split(split_cell_lists_,
							   [&](size_t i)
							   { this->interaction(i, dt * 0.5); });
		}
		/** parallel run the main interaction step between particles. */
		virtual void parallel_runMainStep(Real dt) override
		{
			particle_parallel_for_split(split_cell_lists_,
										[&](size_t i)
										{ this->interaction(i, dt * 0.5); });
		}
	};

	/**
	 * @class InteractionDynamics
	 * @brief This is the class with a single step of particle interaction with other particles
	 */
	template <class LocalDynamicsType>
	class InteractionDynamics : public BaseInteractionDynamics<LocalDynamicsType>
	{
	public:
		template <class BodyRelationType, typename... Args>
		InteractionDynamics(BodyRelationType &body_relation, Args &&...args)
			: InteractionDynamics(true, body_relation, std::forward<Args>(args)...)
		{
			static_assert(!has_initialize<LocalDynamicsType>::value &&
							  !has_update<LocalDynamicsType>::value,
						  "LocalDynamicsType does not fulfill InteractionDynamics requirements");
		};
		virtual ~InteractionDynamics(){};
		/** sequential run the main interaction step between particles. */
		virtual void runMainStep(Real dt) override
		{
			particle_for(this->identifier_.LoopRange(),
						 [&](size_t i)
						 { this->interaction(i, dt); });
		}
		/** parallel run the main interaction step between particles. */
		virtual void parallel_runMainStep(Real dt) override
		{
			particle_parallel_for(this->identifier_.LoopRange(),
								  [&](size_t i)
								  { this->interaction(i, dt); });
		}

	protected:
		template <class BodyRelationType, typename... Args>
		InteractionDynamics(bool mostDerived, BodyRelationType &body_relation, Args &&...args)
			: BaseInteractionDynamics<LocalDynamicsType>(body_relation, std::forward<Args>(args)...){};
	};

	/**
	 * @class InteractionWithUpdate
	 * @brief This class includes an interaction and a update steps
	 */
	template <class LocalDynamicsType>
	class InteractionWithUpdate : public InteractionDynamics<LocalDynamicsType>
	{
	public:
		template <class BodyRelationType, typename... Args>
		InteractionWithUpdate(BodyRelationType &body_relation, Args &&...args)
			: InteractionDynamics<LocalDynamicsType>(false, body_relation, std::forward<Args>(args)...)
		{
			static_assert(!has_initialize<LocalDynamicsType>::value,
						  "LocalDynamicsType does not fulfill InteractionWithUpdate requirements");
		}
		virtual ~InteractionWithUpdate(){};

		virtual void exec(Real dt = 0.0) override
		{
			InteractionDynamics<LocalDynamicsType>::exec(dt);
			particle_for(this->identifier_.LoopRange(),
						 [&](size_t i)
						 { this->update(i, dt); });
		};

		virtual void parallel_exec(Real dt = 0.0) override
		{
			InteractionDynamics<LocalDynamicsType>::parallel_exec(dt);
			particle_parallel_for(this->identifier_.LoopRange(),
								  [&](size_t i)
								  { this->update(i, dt); });
		};
	};

	/**
	 * @class Dynamics1Level
	 * @brief This class includes three steps, including initialization, interaction and update.
	 * It is the most complex particle dynamics type,
	 * and is typically for computing the main fluid and solid dynamics.
	 */
	template <class LocalDynamicsType>
	class Dynamics1Level : public InteractionDynamics<LocalDynamicsType>
	{
	public:
		template <class BodyRelationType, typename... Args>
		Dynamics1Level(BodyRelationType &body_relation, Args &&...args)
			: InteractionDynamics<LocalDynamicsType>(
				  false, body_relation, std::forward<Args>(args)...) {}
		virtual ~Dynamics1Level(){};

		virtual void exec(Real dt = 0.0) override
		{
			this->setUpdated();
			this->setupDynamics(dt);

			particle_for(this->identifier_.LoopRange(),
						 [&](size_t i)
						 { this->initialization(i, dt); });

			InteractionDynamics<LocalDynamicsType>::runInteraction(dt);

			particle_for(this->identifier_.LoopRange(),
						 [&](size_t i)
						 { this->update(i, dt); });
		};

		virtual void parallel_exec(Real dt = 0.0) override
		{
			this->setUpdated();
			this->setupDynamics(dt);

			particle_parallel_for(this->identifier_.LoopRange(),
								  [&](size_t i)
								  { this->initialization(i, dt); });

			InteractionDynamics<LocalDynamicsType>::parallel_runInteraction(dt);

			particle_parallel_for(this->identifier_.LoopRange(),
								  [&](size_t i)
								  { this->update(i, dt); });
		};
	};
}
#endif // PARTICLE_DYNAMICS_ALGORITHMS_H
