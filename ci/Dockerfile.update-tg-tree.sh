#! /bin/bash
#
# Generate a Dockerfile, build it and run update-tg-tree.sh script

# We should manage all errors in this script
set -e

SCRIPT="${1:-./update-tg-tree.sh}"
if ! test -f "${SCRIPT}"; then
    echo "Unable to execute ${SCRIPT} from ${PWD}"
    exit 1
fi
shift
SCRIPT_OPTS=("${@}")

DOCKER_NAME="update-tg-tree"
DOCKER_DIR=$(dirname "${0}")

TG_SETUP_URL="https://github.com/mackyle/topgit/releases/download/topgit-0.19.12/topgit-0.19.12.tar.gz"
TG_SETUP_TARBALL="topgit.tar.gz"

DOCKERFILE=$(mktemp --tmpdir="${DOCKER_DIR}")
trap 'rm -f "${DOCKERFILE}"' EXIT

cat <<EOF > "${DOCKERFILE}"
FROM ubuntu:latest

# Use the same rights as the launcher
RUN mkdir -p "$(dirname "${HOME}")" && \
    useradd -ms /bin/bash -u "${UID}" -U "${USER}" -d "${HOME}"

# dependencies for the script
RUN apt-get update && \
    apt-get install -y build-essential libncurses5-dev gcc libssl-dev bc bison \
                       libelf-dev flex git curl tar hashalot ccache && \
    apt-get clean

# TopGit
RUN curl -L "${TG_SETUP_URL}" -o "${TG_SETUP_TARBALL}" && \
    tar xzf "${TG_SETUP_TARBALL}" && \
    cd "topgit-"* && \
    make prefix="/usr" install && \
    cd .. && \
    rm -rf "${TG_SETUP_TARBALL}" "topgit-"*

# CCache for quicker builds with default colours
# Note: use 'ccache -M xG' to increase max size, default is 5GB
ENV PATH /usr/lib/ccache:\${PATH}
ENV CCACHE_COMPRESS true
ENV GCC_COLORS error=01;31:warning=01;35:note=01;36:caret=01;32:locus=01:quote=01

# switch to the current user and current dir
USER ${USER}
VOLUME ${PWD}
WORKDIR ${PWD}
EOF

docker build -t "${DOCKER_NAME}" -f "${DOCKERFILE}" "${DOCKER_DIR}"
# ssh and gitconfig is needed to create commits and pull/push
docker run \
    --init \
    -e "UPD_TG_FORCE_SYNC=${UPD_TG_FORCE_SYNC}" \
    -e "UPD_TG_NOT_BASE=${UPD_TG_NOT_BASE}" \
    -v "${PWD}:${PWD}" \
    -v "${HOME}/.ssh:${HOME}/.ssh" \
    -v "${HOME}/.gitconfig:${HOME}/.gitconfig:ro" \
    -v "${HOME}/.ccache:${HOME}/.ccache" \
    --rm "${DOCKER_NAME}" \
    bash "-${-}" "${SCRIPT}" "${SCRIPT_OPTS[@]}"
