# SPDX-License-Identifier: LicenseRef-CSSL-1.0

#---------------------------------------------------------------------
# Python for CI of OAI-eNB + COTS-UE
#
#   Required Python Version
#     Python 3.x
#---------------------------------------------------------------------

#-----------------------------------------------------------
# Import
#-----------------------------------------------------------
import re	       # reg
import logging
import os
from typing import NamedTuple

#-----------------------------------------------------------
# OAI Testing modules
#-----------------------------------------------------------
import cls_cmd
import cls_analysis
from cls_ci_helper import archiveArtifact

#-----------------------------------------------------------
# Helper functions used here and in other classes
# (e.g., cls_cluster.py)
#-----------------------------------------------------------
IMAGES = ['oai-enb', 'oai-lte-ru', 'oai-lte-ue', 'oai-gnb', 'oai-nr-cuup', 'oai-gnb-aw2s', 'oai-nr-ue', 'oai-enb-asan', 'oai-gnb-asan', 'oai-lte-ue-asan', 'oai-nr-ue-asan', 'oai-nr-cuup-asan', 'oai-gnb-aerial', 'oai-gnb-fhi72', 'oai-gnb-fhi72-t2', 'oai-nr-oru']
DEFAULT_REGISTRY = "gracehopper3-oai.sboai.cs.eurecom.fr"

def CreateWorkspace(host, sourcePath, repository, branch):
	script = "scripts/create_workspace.sh"
	options = f"{sourcePath} {repository} {branch}"
	logging.info(f'execute "{script}" with options "{options}" on node {host}')
	with cls_cmd.getConnection(host) as c:
		ret = c.exec_script(script, 90, options)
	logging.debug(f'"{script}" finished with code {ret.returncode}, output:\n{ret.stdout}')
	return ret.returncode == 0

def AnalyzeBuildLogs(image, lf):
	committed = False
	tagged = False
	errors = []
	with open(lf, mode='r') as inputfile:
		for line in inputfile:
			lineHasTag = re.search(f'Successfully tagged {image}:', str(line)) is not None
			lineHasTag2 = re.search(f'naming to docker.io/library/{image}:', str(line)) is not None
			tagged = tagged or lineHasTag or lineHasTag2
			# the OpenShift Cluster builder prepends image registry URL
			lineHasCommit = re.search(r'COMMIT [a-zA-Z0-9\.:/\-]*' + image, str(line)) is not None
			committed = committed or lineHasCommit
			# ignore apt errors, if it installes, it's good
			if re.search(r'error:|Errors|ERROR', line) and not re.search(r'update-alternatives: error: alternative', line):
				errors.append(f"=> {line.strip()}")
	status = (committed or tagged) and len(errors) == 0
	logging.info(f"Analyzing {image}, file {lf}: {status=}, {len(errors)} errors")
	for e in errors:
		logging.info(e)
	return status, errors

def GetImageName(ssh, svcName, file):
	ret = ssh.run(f"docker compose -f {file} config --format json {svcName}  | jq -r '.services.\"{svcName}\".image'", silent=True)
	if ret.returncode != 0:
		return f"cannot retrieve image info for {containerName}: {ret.stdout}"
	else:
		return ret.stdout.strip()

def ExistEnvFilePrint(ssh, wd, prompt='env vars in existing'):
	ret = ssh.run(f'cat {wd}/.env', silent=True, reportNonZero=False)
	if ret.returncode != 0:
		return False
	env_vars = ret.stdout.strip().splitlines()
	logging.info(f'{prompt} {wd}/.env: {env_vars}')
	return True

def WriteEnvFile(ssh, services, wd, tag, flexric_tag):
	ret = ssh.run(f'cat {wd}/.env', silent=True, reportNonZero=False)
	registry = "oai-ci/" # pull_images() gives us this registry path
	envs = {"REGISTRY":registry, "TAG": tag, "FLEXRIC_TAG": flexric_tag}
	if ret.returncode == 0: # it exists, we have to update
		# transforms env file to dictionary
		old_envs = {}
		for l in ret.stdout.strip().splitlines():
			var, val = l.split('=', 1)
			old_envs[var] = val.strip('"')
		# will retain the old environment variables
		envs = {**envs, **old_envs}
	for svc in services.split():
		# In some scenarios we have the choice of either pulling normal images
		# or -asan images. We need to detect which kind we did pull.
		fullImageName = GetImageName(ssh, svc, f"{wd}/docker-compose.y*ml")
		image = fullImageName.split("/")[-1].split(":")[0]
		# registry now includes the trailing slash ("oai-ci/")
		checkimg = f"{registry}{image}-asan:{tag}"
		ret = ssh.run(f'docker image inspect {checkimg}', reportNonZero=False)
		if ret.returncode == 0:
			logging.info(f"detected pulled image {checkimg}")
			if "oai-enb" in image: envs["ENB_IMG"] = "oai-enb-asan"
			elif "oai-gnb" in image: envs["GNB_IMG"] = "oai-gnb-asan"
			elif "oai-lte-ue" in image: envs["LTEUE_IMG"] = "oai-lte-ue-asan"
			elif "oai-nr-ue" in image: envs["NRUE_IMG"] = "oai-nr-ue-asan"
			elif "oai-nr-cuup" in image: envs["NRCUUP_IMG"] = "oai-nr-cuup-asan"
			else: logging.warning("undetected image format {image}, cannot use asan")
	env_string = "\n".join([f"{var}=\"{val}\"" for var,val in envs.items()])
	ssh.run(f'echo -e \'{env_string}\' > {wd}/.env', silent=True)
	ExistEnvFilePrint(ssh, wd, prompt='New env vars in file')

def GetServices(ssh, requested, file):
    if requested == [] or requested is None or requested == "":
        logging.warning('No service name given: starting all services in docker-compose.yml!')
        ret = ssh.run(f'docker compose -f {file} config --services')
        if ret.returncode != 0:
            return ""
        else:
            return ' '.join(ret.stdout.splitlines())
    else:
        return requested

def CopyinServiceLog(ssh, lSourcePath, svcName, wd_yaml, ctx):
	remote_filename = f"{lSourcePath}/cmake_targets/log/{svcName}.logs"
	ssh.run(f'docker compose -f {wd_yaml} logs {svcName} --no-log-prefix &> {remote_filename}')
	return archiveArtifact(ssh, ctx, remote_filename)

def GetDeployedServices(ssh, file):
	ret = ssh.run(f'docker compose -f {file} --profile \'*\' config --services')
	if ret.returncode != 0:
		logging.error("could not get services")
		return None
	allServices = ret.stdout.splitlines()
	deployed_services = []
	for s in allServices:
		# outputs the hash if the container has been deployed (but might be stopped)
		ret = ssh.run(f'docker compose -f {file} ps --all --quiet -- {s}', silent=True)
		if ret.returncode != 0:
			# error: should not happen as we iterate over docker-provided service list
			logging.error(f"service {s}: {ret.stdout}")
		elif ret.stdout == "":
			logging.info(f"service {s} not deployed")
		else:
			c = ret.stdout
			logging.info(f'service {s} with container id {c}')
			deployed_services.append((s, c))
	return deployed_services

class DeploymentOpt(NamedTuple):
	"""Options to deploy/stop/undeploy docker compose services"""
	yaml_path: str # directory of docker-compose.yml, relative to workspace
	services: str = '' # space-separated list of services, empty for all
	num_attempts: int = 1 # number of deployment attempts

#-----------------------------------------------------------
# Class Declaration
#-----------------------------------------------------------
class Containerize():

#-----------------------------------------------------------
# Container management functions
#-----------------------------------------------------------

	def BuildImage(ctx, node, HTML, image_kind):
		lSourcePath = ctx.g.workspace
		logging.debug('Building on server: ' + node)
		cmd = cls_cmd.getConnection(node)
		log_files = []
	
		dockerfileprefix = '.ubuntu'

		baseImage = 'ran-base'
		baseTag = 'develop'
		buildImage = 'ran-build'
		forceBaseImageBuild = False
		imageTag = 'develop'

		result = re.search('native_cuda_armv8', image_kind)
		if result is not None:
			baseImage = 'ran-base-cuda'
			buildImage = 'ran-build-cuda'
			dockerfileprefix = '.cuda.ubuntu'
		# we always build the ran-build image with all targets
		# Creating a tupple with the imageName, the DockerFile prefix pattern, targetName and sanitized option
		imageNames = [(buildImage, 'build', f'{buildImage}', '')]
		result = re.search('eNB', image_kind)
		if result is not None:
			imageNames.append(('oai-enb', 'eNB', 'oai-enb', ''))
		result = re.search('gNB', image_kind)
		if result is not None:
			imageNames.append(('oai-gnb', 'gNB', 'oai-gnb', ''))
		result = re.search('x86', image_kind)
		if result is not None:
			imageNames.append(('oai-enb', 'eNB', 'oai-enb', ''))
			imageNames.append(('oai-gnb', 'gNB', 'oai-gnb', ''))
			imageNames.append(('oai-nr-cuup', 'nr-cuup', 'oai-nr-cuup', ''))
			imageNames.append(('oai-lte-ue', 'lteUE', 'oai-lte-ue', ''))
			imageNames.append(('oai-nr-ue', 'nrUE', 'oai-nr-ue', ''))
			imageNames.append(('oai-lte-ru', 'lteRU', 'oai-lte-ru', ''))
			# Building again the 5G images with Address Sanitizer
			imageNames.append(('ran-build', 'build', 'ran-build-asan', '--build-arg "BUILD_OPTION=--sanitize"'))
			imageNames.append(('oai-enb', 'eNB', 'oai-enb-asan', '--build-arg "BUILD_OPTION=--sanitize"'))
			imageNames.append(('oai-gnb', 'gNB', 'oai-gnb-asan', '--build-arg "BUILD_OPTION=--sanitize"'))
			imageNames.append(('oai-lte-ue', 'lteUE', 'oai-lte-ue-asan', '--build-arg "BUILD_OPTION=--sanitize"'))
			imageNames.append(('oai-nr-ue', 'nrUE', 'oai-nr-ue-asan', '--build-arg "BUILD_OPTION=--sanitize"'))
			imageNames.append(('oai-nr-cuup', 'nr-cuup', 'oai-nr-cuup-asan', '--build-arg "BUILD_OPTION=--sanitize"'))
			imageNames.append(('ran-build-fhi72', 'build.fhi72', 'ran-build-fhi72', ''))
			imageNames.append(('oai-gnb', 'gNB.fhi72', 'oai-gnb-fhi72', ''))
			imageNames.append(('oai-nr-oru', 'nrORU.fhi72', 'oai-nr-oru', ''))
		result = re.search('build_cross_arm64', image_kind)
		if result is not None:
			dockerfileprefix = '.ubuntu.cross-arm64'
		result = re.search('native_armv9', image_kind)
		if result is not None:
			imageNames.append(('oai-gnb', 'gNB', 'oai-gnb', ''))
			imageNames.append(('ran-build-fhi72', 'build.fhi72.native_arm', 'ran-build-fhi72', ''))
			imageNames.append(('oai-nr-cuup', 'nr-cuup', 'oai-nr-cuup', ''))
			imageNames.append(('oai-nr-ue', 'nrUE', 'oai-nr-ue', ''))
			imageNames.append(('oai-gnb-aerial', 'gNB.aerial', 'oai-gnb-aerial', ''))
		result = re.search('native_armv8', image_kind)
		if result is not None:
			imageNames.append(('oai-gnb', 'gNB', 'oai-gnb', ''))
			imageNames.append(('oai-nr-cuup', 'nr-cuup', 'oai-nr-cuup', ''))
			imageNames.append(('oai-nr-ue', 'nrUE', 'oai-nr-ue', ''))
		result = re.search('fhi72-t2', image_kind)
		if result is not None:
			imageNames.append(('ran-build-fhi72-t2', 'build.fhi72.t2', 'ran-build-fhi72-t2', ''))
			imageNames.append(('oai-gnb', 'gNB.fhi72.t2', 'oai-gnb-fhi72-t2', ''))
		result = re.search('native_cuda_armv8', image_kind)
		if result is not None:
			imageNames.append(('oai-gnb', 'gNB', 'oai-gnb', ''))
			imageNames.append(('oai-nr-ue', 'nrUE', 'oai-nr-ue', ''))

		cmd.cd(lSourcePath)

		if ctx.g.merge:
			imageTag = 'ci-temp'
			if ctx.g.targetBranch == 'develop':
				cmd.run(f'git diff HEAD..origin/develop -- cmake_targets/build_oai cmake_targets/tools/build_helper docker/Dockerfile.base{dockerfileprefix} | grep --colour=never -i INDEX')
				result = re.search('index', cmd.getBefore())
				if result is not None:
					forceBaseImageBuild = True
					baseTag = 'ci-temp'
			# if the branch name contains integration_20xx_wyy, let rebuild ran-base
			result = re.search('integration_20([0-9]{2})_w([0-9]{2})', ctx.g.branch)
			if not forceBaseImageBuild and result is not None:
				forceBaseImageBuild = True
				baseTag = 'ci-temp'
		else:
			forceBaseImageBuild = True

		# Let's remove any previous run artifacts if still there
		cmd.run(f"docker image prune --force")
		for image,pattern,name,option in imageNames:
			cmd.run(f"docker image rm {name}:{imageTag}", reportNonZero=False)

		cmd.run(f'docker login -u oaicicd -p oaicicd {DEFAULT_REGISTRY}')
		ubuntuImage = "ubuntu:noble"
		# Build the base image only on Push Events (not on Merge Requests)
		# On when the base image docker file is being modified.
		if forceBaseImageBuild:
			cmd.run(f"docker image rm {baseImage}:{baseTag}")
			logfile = f'{lSourcePath}/cmake_targets/log/{baseImage}.docker.log'
			option = f" --build-arg UBUNTU_IMAGE={DEFAULT_REGISTRY}/{ubuntuImage}"
			cmd.run(f"docker build --target {baseImage} --tag {baseImage}:{baseTag} --file docker/Dockerfile.base{dockerfileprefix} {option} . &> {logfile}", timeout=1600)
			t = (baseImage, archiveArtifact(cmd, ctx, logfile))
			log_files.append(t)

		ret = cmd.run(f"docker image inspect --format=\'Size = {{{{.Size}}}} bytes\' {baseImage}:{baseTag}")

		allImagesSize = {}
		if ret.returncode != 0:
			logging.error(f'\u001B[1m Could not build properly {baseImage}\u001B[0m')
			# Recover the name of the failed container?
			cmd.run(f"docker ps --quiet --filter \"status=exited\" -n1 | xargs --no-run-if-empty docker rm -f")
			cmd.run(f"docker image prune --force")
			cmd.close()
			logging.error('\u001B[1m Building OAI Images Failed\u001B[0m')
			HTML.CreateHtmlTestRowQueue(image_kind, 'KO', [])
			return False
		else:
			result = re.search(r'Size *= *(?P<size>[0-9\-]+) *bytes', cmd.getBefore())
			if result is not None:
				size = float(result.group("size")) / 1000000
				imageSizeStr = f'{size:.1f}'
				logging.debug(f'\u001B[1m {baseImage} size is {imageSizeStr} Mbytes\u001B[0m')
				allImagesSize[baseImage] = f'{imageSizeStr} Mbytes'
			else:
				logging.debug(f'{baseImage} size is unknown')

		# Build the target image(s)
		status = True
		for image,pattern,name,option in imageNames:
			# the archived Dockerfiles have "ran-base:latest" as base image
			# we need to update them with proper tag
			cmd.run(f'git checkout -- docker/Dockerfile.{pattern}{dockerfileprefix}')
			cmd.run(f'sed -i -e "s#{baseImage}:latest#{baseImage}:{baseTag}#" docker/Dockerfile.{pattern}{dockerfileprefix}')
			# target images should use the proper ran-build image
			if image != 'ran-build' and "-asan" in name:
				cmd.run(f'sed -i -e "s#{buildImage}:latest#{buildImage}-asan:{imageTag}#" docker/Dockerfile.{pattern}{dockerfileprefix}')
			elif "fhi72" in name or name == "oai-nr-oru":
				cmd.run(f'sed -i -e "s#ran-build-fhi72:latest#ran-build-fhi72:{imageTag}#" docker/Dockerfile.{pattern}{dockerfileprefix}')
			elif image != 'ran-build':
				cmd.run(f'sed -i -e "s#{buildImage}:latest#{buildImage}:{imageTag}#" docker/Dockerfile.{pattern}{dockerfileprefix}')
			if image == 'oai-gnb-aerial':
				cmd.run('cp -f /opt/nvidia-ipc/nvipc_src.2026.03.04.tar.gz .')
			if image == 'ran-build-fhi72-t2':
				cmd.run('cp -f /opt/t2-patch/AMD-T2-SDFEC_25-03-1.patch .')
			if name == 'oai-gnb-fhi72-t2':
				cmd.run(f'sed -i -e "s#ran-build-fhi72-t2:latest#ran-build-fhi72-t2:{imageTag}#" docker/Dockerfile.{pattern}{dockerfileprefix}')
			logfile = f'{lSourcePath}/cmake_targets/log/{name}.docker.log'
			option = option + f" --build-arg UBUNTU_IMAGE={DEFAULT_REGISTRY}/{ubuntuImage}"
			ret = cmd.run(f'docker build --target {image} --tag {name}:{imageTag} --file docker/Dockerfile.{pattern}{dockerfileprefix} {option} . > {logfile} 2>&1', timeout=1200)
			t = (name, archiveArtifact(cmd, ctx, logfile))
			log_files.append(t)
			if image == 'oai-gnb-aerial':
				cmd.run('rm -f nvipc_src.2026.01.07.tar.gz')
			if image == 'ran-build-fhi72-t2':
				cmd.run('rm -f AMD-T2-SDFEC_25-03-1.patch')
			# check the status of the build
			ret = cmd.run(f"docker image inspect --format=\'Size = {{{{.Size}}}} bytes\' {name}:{imageTag}")
			if ret.returncode != 0:
				logging.error('\u001B[1m Could not build properly ' + name + '\u001B[0m')
				status = False
				# Here we should check if the last container corresponds to a failed command and destroy it
				cmd.run(f"docker ps --quiet --filter \"status=exited\" -n1 | xargs --no-run-if-empty docker rm -f")
				allImagesSize[name] = 'N/A -- Build Failed'
				break
			else:
				result = re.search(r'Size *= *(?P<size>[0-9\-]+) *bytes', cmd.getBefore())
				if result is not None:
					size = float(result.group("size")) / 1000000 # convert to MB
					imageSizeStr = f'{size:.1f}'
					logging.debug(f'\u001B[1m   {name} size is {imageSizeStr} Mbytes\u001B[0m')
					allImagesSize[name] = f'{imageSizeStr} Mbytes'
				else:
					logging.debug(f'{name} size is unknown')
					allImagesSize[name] = 'unknown'
			# Now pruning dangling images in between target builds
			cmd.run(f"docker image prune --force")
		cmd.run(f'docker logout {DEFAULT_REGISTRY}')
		# Remove all intermediate build images and clean up
		cmd.run(f"docker image rm ran-build:{imageTag} ran-build-asan:{imageTag} ran-build-fhi72:{imageTag} || true")
		cmd.run(f"docker volume prune --force")

		# Remove some cached artifacts to prevent out of diskspace problem
		logging.debug(cmd.run("df -h").stdout)
		logging.debug(cmd.run("docker system df").stdout)
		cmd.run(f"docker buildx prune --filter until=1h --force")
		logging.debug(cmd.run("df -h").stdout)
		logging.debug(cmd.run("docker system df").stdout)

		cmd.close()

		# Analyze the logs
		for name, lf in log_files:
			imgStatus, errors = AnalyzeBuildLogs(name, lf)
			info = f"Analysis of {os.path.basename(lf)}: {imgStatus=}, size {allImagesSize[name]}, {len(errors)} errors"
			msg = "\n".join([info] + errors)
			HTML.CreateHtmlTestRowQueue(name, 'OK' if imgStatus else 'KO', [msg])
			status = status and imgStatus
		
		if status:
			logging.info('\u001B[1m Building OAI Image(s) Pass\u001B[0m')
		else:
			logging.error('\u001B[1m Building OAI Images Failed\u001B[0m')
		return status

	def BuildRunTests(ctx, node, dockerfile, runtime_opt, ctest_opt, HTML):
		lSourcePath = ctx.g.workspace
		logging.debug('Building on server: ' + node)
		cmd = cls_cmd.getConnection(node)
		cmd.cd(lSourcePath)

		# check that ran-base image exists as we expect it
		baseImage = 'ran-base'
		baseTag = 'develop'
		if ctx.g.merge:
			if ctx.g.targetBranch == 'develop':
				cmd.run(f'git diff HEAD..origin/develop -- cmake_targets/build_oai cmake_targets/tools/build_helper docker/Dockerfile.base.ubuntu | grep --colour=never -i INDEX')
				result = re.search('index', cmd.getBefore())
				if result is not None:
					baseTag = 'ci-temp'
		ret = cmd.run(f"docker image inspect --format=\'Size = {{{{.Size}}}} bytes\' {baseImage}:{baseTag}")
		if ret.returncode != 0:
			logging.error(f'No {baseImage} image present, cannot build tests')
			HTML.CreateHtmlTestRowQueue("Unit test build failed", 'KO', [])
			cmd.close()
			return False

		# build ran-unittests image
		logfile = f'{lSourcePath}/cmake_targets/log/unittest-build.log'
		ret = cmd.run(f'docker build --progress=plain --tag ran-unittests:{baseTag} --file ci-scripts/{dockerfile} . &> {logfile}')

		archiveArtifact(cmd, ctx, logfile)
		if ret.returncode != 0:
			logging.error(f'Cannot build unit tests')
			HTML.CreateHtmlTestRowQueue("Unit test build failed", 'KO', [dockerfile])
			cmd.run(f'docker image rm --force ran-unittests:{baseTag}')
			cmd.close()
			return False

		HTML.CreateHtmlTestRowQueue("Build unit tests", 'OK', [dockerfile])

		# it worked, build and execute tests, and close connection
		# I would like to run it with --rm and mount the ctest result directory to avoid 'docker cp'
		# below, but then permissions are messed up and we can't remove the directory without sudo
		# making the next pipeline fail
		cmd.run('docker rm --force --volumes ran-unittests')
		ret = cmd.run(f'docker run -a STDOUT {runtime_opt} --shm-size=2g --workdir /oai-ran/build/ --env LD_LIBRARY_PATH=/oai-ran/build/ --name ran-unittests ran-unittests:{baseTag} ctest --no-label-summary -j$(nproc) {ctest_opt}')
		cmd.run('docker cp ran-unittests:/oai-ran/build/Testing/Temporary/LastTest.log .')
		archiveArtifact(cmd, ctx, f'{lSourcePath}/LastTest.log')
		cmd.run('docker cp ran-unittests:/oai-ran/build/Testing/Temporary/LastTestsFailed.log .')
		archiveArtifact(cmd, ctx, f'{lSourcePath}/LastTestsFailed.log')
		cmd.run('docker rm ran-unittests')
		cmd.run(f'docker image rm ran-unittests:{baseTag}')
		cmd.run(f"docker volume prune --force")
		cmd.run(f"docker buildx prune --filter until=1h --force")
		logging.debug(cmd.run("df -h").stdout)
		logging.debug(cmd.run("docker system df").stdout)
		cmd.close()

		if ret.returncode == 0:
			HTML.CreateHtmlTestRowQueue('Unit tests succeeded', 'OK', [ret.stdout])
			return True
		else:
			HTML.CreateHtmlTestRowQueue('Unit tests failed (see also doc/UnitTests.md)', 'KO', [ret.stdout])
			return False

	def Push_Image_to_Local_Registry(ctx, node, HTML, tag_prefix=""):
		logging.debug('Pushing images to server: ' + node)
		ssh = cls_cmd.getConnection(node)
		imagePrefix = DEFAULT_REGISTRY
		ret = ssh.run(f'docker login -u oaicicd -p oaicicd {imagePrefix}')
		if ret.returncode != 0:
			msg = 'Could not log into local registry'
			logging.error(msg)
			ssh.close()
			HTML.CreateHtmlTestRowQueue(msg, 'KO', [])
			return False

		orgTag = 'develop'
		if ctx.g.merge:
			orgTag = 'ci-temp'
		for image in IMAGES:
			tagToUse = tag_prefix + ctx.g.branch
			imageTag = f"{image}:{tagToUse}"
			ret = ssh.run(f'docker image tag {image}:{orgTag} {imagePrefix}/{imageTag}')
			if ret.returncode != 0:
				continue
			ret = ssh.run(f'docker push {imagePrefix}/{imageTag}')
			if ret.returncode != 0:
				msg = f'Could not push {image} to local registry : {imageTag}'
				logging.error(msg)
				ssh.close()
				HTML.CreateHtmlTestRowQueue(msg, 'KO', [])
				return False
			# Creating a develop tag on the local private registry
			if not ctx.g.merge:
				devTag = f"{tag_prefix}develop"
				ssh.run(f'docker image tag {image}:{orgTag} {imagePrefix}/{image}:{devTag}')
				ssh.run(f'docker push {imagePrefix}/{image}:{devTag}')
				ssh.run(f'docker rmi {imagePrefix}/{image}:{devTag}')
			ssh.run(f'docker rmi {imagePrefix}/{imageTag} {image}:{orgTag}')

		ret = ssh.run(f'docker logout {imagePrefix}')
		if ret.returncode != 0:
			msg = 'Could not log off from local registry'
			logging.error(msg)
			ssh.close()
			HTML.CreateHtmlTestRowQueue(msg, 'KO', [])
			return False

		ssh.close()
		HTML.CreateHtmlTestRowQueue('N/A', 'OK', [])
		return True

	def Pull_Image(cmd, images, tag, tag_prefix, registry, username, password):
		if username is not None and password is not None:
			logging.info(f"logging into registry {username}@{registry}")
			response = cmd.run(f'docker login -u {username} -p {password} {registry}', silent=True, reportNonZero=False)
			if response.returncode != 0:
				msg = f'Could not log into registry {username}@{registry}'
				logging.error(msg)
				return False, msg
		pulled_images = []
		for image in images:
			imagePrefTag = f"{image}:{tag_prefix}{tag}"
			imageTag = f"{image}:{tag}"
			response = cmd.run(f'docker pull {registry}/{imagePrefTag}')
			if response.returncode != 0:
				msg = f'Could not pull {image} from local registry: {imagePrefTag}'
				logging.error(msg)
				return False, msg
			cmd.run(f'docker tag {registry}/{imagePrefTag} oai-ci/{imageTag}')
			cmd.run(f'docker rmi {registry}/{imagePrefTag}')
			pulled_images += [f"oai-ci/{imageTag}"]
		if username is not None and password is not None:
			response = cmd.run(f'docker logout {registry}')
			# we have the images, if logout fails it's no problem
		msg = "Pulled Images:\n" + '\n'.join(pulled_images)
		return True, msg

	def Pull_Image_from_Registry(ctx, HTML, node, images, tag=None, tag_prefix="", registry=DEFAULT_REGISTRY, username="oaicicd", password="oaicicd"):
		logging.debug(f'\u001B[1m Pulling image(s) on server: {node}\u001B[0m')
		if not tag:
			tag = ctx.g.branch
		with cls_cmd.getConnection(node) as cmd:
			success, msg = Containerize.Pull_Image(cmd, images, tag, tag_prefix, registry, username, password)
		param = f"on node {node}"
		if success:
			HTML.CreateHtmlTestRowQueue(param, 'OK', [msg])
		else:
			HTML.CreateHtmlTestRowQueue(param, 'KO', [msg])
		return success

	def Clean_Test_Server_Images(ctx, HTML, node, images, tag=None):
		logging.debug(f'\u001B[1m Cleaning image(s) from server: {node}\u001B[0m')
		if not tag:
			tag = ctx.g.branch

		status = True
		with cls_cmd.getConnection(node) as myCmd:
			removed_images = []
			for image in images:
				fullImage = f"oai-ci/{image}:{tag}"
				cmd = f'docker rmi {fullImage}'
				if myCmd.run(cmd).returncode != 0:
					status = False
				removed_images += [fullImage]

		msg = "Removed Images:\n" + '\n'.join(removed_images)
		s = 'OK' if status else 'KO'
		param = f"on node {node}"
		HTML.CreateHtmlTestRowQueue(param, s, [msg])
		return status

	def Create_Workspace(ctx, node, HTML):
		sourcePath = ctx.g.workspace
		success = CreateWorkspace(node, sourcePath, ctx.g.repository, ctx.g.branch)
		if success:
			HTML.CreateHtmlTestRowQueue('N/A', 'OK', [f"created workspace {sourcePath} on node {node}"])
		else:
			HTML.CreateHtmlTestRowQueue('N/A', 'KO', ["cannot create workspace"])
		return success

	def DeployObject(ctx, node, HTML, opt):
		num_attempts = opt.num_attempts
		lSourcePath = ctx.g.workspace
		yaml = opt.yaml_path.strip('/')
		wd = f'{lSourcePath}/{yaml}'
		wd_yaml = f'{wd}/docker-compose.y*ml'
		with cls_cmd.getConnection(node) as ssh:
			services = GetServices(ssh, opt.services, wd_yaml)
			if services == [] or services == ' ' or services == None:
				msg = 'Cannot determine services to start'
				logging.error(msg)
				HTML.CreateHtmlTestRowQueue('N/A', 'KO', [msg])
				return False
			logging.info(f'\u001B[1mDeploying object(s) "{services}" on server {node}\u001B[0m')
			ExistEnvFilePrint(ssh, wd)
			WriteEnvFile(ssh, services, wd, ctx.g.branch, ctx.g.flexric_tag)
			if num_attempts <= 0:
				raise ValueError(f'Invalid value for num_attempts: {num_attempts}, must be greater than 0')
			for attempt in range(num_attempts):
				logging.info(f'will start services {services}')
				status = ssh.run(f'docker compose -f {wd_yaml} up -d --wait --wait-timeout 120 -- {services}')
				info = ssh.run(f"docker compose -f {wd_yaml} ps --all --format=\'table {{{{.Service}}}} [{{{{.Image}}}}] {{{{.Status}}}}\' -- {services} | column -t")
				deployed = status.returncode == 0
				if not deployed:
					msg = f'cannot deploy services {services}: {status.stdout}'
					logging.error(msg)
				else:
					break
				if (attempt < num_attempts - 1):
					warning_msg = (f'Failed to deploy on attempt {attempt}, restart services {services}')
					logging.warning(warning_msg)
					HTML.CreateHtmlTestRowQueue('N/A', 'NOK', [warning_msg])
					for svc in services.split():
						CopyinServiceLog(ssh, lSourcePath, svc, wd_yaml, ctx)
					ssh.run(f'docker compose -f {wd_yaml} down -- {services}')
		imagesInfo = info.stdout.splitlines()[1:]
		logging.debug(f'{info.stdout.splitlines()[1:]}')
		if deployed:
			HTML.CreateHtmlTestRowQueue(opt.services, 'OK', ['\n'.join(imagesInfo)])
			logging.info('\u001B[1m Deploying objects Pass\u001B[0m')
		else:
			HTML.CreateHtmlTestRowQueue(opt.services, 'KO', ['\n'.join(imagesInfo)])
			logging.error('\u001B[1m Deploying objects Failed\u001B[0m')
		return deployed

	def StopObject(ctx, node, HTML, opt):
		lSourcePath = ctx.g.workspace
		if not opt.services:
			raise ValueError(f'no services provided')
		logging.info(f'\u001B[1m Stopping objects "{opt.services}" from server: {node}\u001B[0m')
		reqServices = opt.services.split()
		yaml = opt.yaml_path.strip('/')
		wd = f'{lSourcePath}/{yaml}'
		wd_yaml = f'{wd}/docker-compose.y*ml'
		with cls_cmd.getConnection(node) as ssh:
			ExistEnvFilePrint(ssh, wd)
			services = [s for s, _ in GetDeployedServices(ssh, wd_yaml)]
			success = []
			fail = []
			for s in reqServices:
				if s in services:
					ssh.run(f'docker compose -f {wd_yaml} stop -- {s}')
					success.append(s)
				else:
					logging.error(f"no such service {s}")
					fail.append(s)
		if success == reqServices:
			logging.info('\u001B[1m Stopping object Pass\u001B[0m')
			HTML.CreateHtmlTestRowQueue(opt.services, 'OK', [f'Stopped {opt.services}'])
		else:
			logging.error('\u001B[1m Stopping object Failed\u001B[0m')
			HTML.CreateHtmlTestRowQueue(opt.services, 'KO', [f'Failed stopping {" ".join(fail)}, succeeded {" ".join(success)}'])
		return success

	def UndeployObject(ctx, node, HTML, opt, to_analyze):
		lSourcePath = ctx.g.workspace
		logging.info(f'\u001B[1m Undeploying all objects from server {node}\u001B[0m')
		yaml = opt.yaml_path.strip('/')
		wd = f'{lSourcePath}/{yaml}'
		wd_yaml = f'{wd}/docker-compose.y*ml'
		with cls_cmd.getConnection(node) as ssh:
			ExistEnvFilePrint(ssh, wd)
			services = GetDeployedServices(ssh, wd_yaml)
			all_logs = True
			ssh.run(f'docker compose -f {wd_yaml} --profile \'*\' stop')
			if services is not None:
				service_desc = {}
				for s, c in services:
					ret = ssh.run(f'docker inspect {c} --format="{{{{.State.ExitCode}}}}"')
					rc = int(ret.stdout.strip())
					f = CopyinServiceLog(ssh, lSourcePath, s, wd_yaml, ctx)
					all_logs = all_logs and f is not None
					service_desc[s] = {'returncode': rc, 'logfile': f}
			else:
				logging.warning('could not identify services to stop => no log file')
			ssh.run(f'docker compose -f {wd_yaml} --profile \'*\' down -v')
			HTML.CreateHtmlTestRowQueue(node, 'OK', ['Undeployment successful'])
			ssh.run(f'rm {wd}/.env')
		if not all_logs:
			HTML.CreateHtmlTestRowQueue('N/A', 'KO', ['Could not copy logfile(s)'])
			logging.error(f"could not copy all files: {all_logs=} {services=}")
			success = False
		else:
			success = cls_analysis.AnalyzeServices(HTML, service_desc, to_analyze)
		if success:
			logging.info('\u001B[1m Undeploying objects Pass\u001B[0m')
		else:
			logging.error('\u001B[1m Undeploying objects Failed\u001B[0m')
		return success

	def AnalyzeRTStatsObject(HTML, node, ctx, yaml_path, thresholds, service=None, stats_files=None):
		logging.info(f'Analyzing realtime stats from server: {node}')
		yaml = yaml_path.strip('/')
		wd = f'{ctx.g.workspace}/{yaml}'
		wd_yaml = f'{wd}/docker-compose.y*ml'

		with cls_cmd.getConnection(node) as cmd:
			services = GetDeployedServices(cmd, wd_yaml)
			if not services:
				raise RuntimeError("No deployed docker compose services found")
			deployed_services = [s for s, _ in services]
			s = service or deployed_services[0] # choose first service if not provided
			if s not in deployed_services:
				raise RuntimeError(f"Requested service {s} not found among services: {deployed_services}")
			logging.info(f"Analyzing deployed service '{s}'")
			# similar to BuildRunTests(), use docker cp to avoid problems with permissions
			local_files = []
			for sf in stats_files:
				basename = os.path.basename(sf)
				ret = cmd.run(f'docker compose -f {wd_yaml} cp {s}:{sf} {wd}/')
				if ret.returncode != 0:
					logging.error(f"Cannot retrieve {s}:{sf}")
					return False
				file = archiveArtifact(cmd, ctx, f"{wd}/{basename}")
				if not file:
					logging.error(f"Cannot retrieve file {basename}")
					return False
				local_files.append(file)

		logging.info(f"check against thresholds from {thresholds}")
		success, datalog_rt_stats = cls_analysis.Analysis.analyze_rt_stats(thresholds, local_files)
		HTML.CreateHtmlDataLogTable(datalog_rt_stats, thresholds)
		return success
