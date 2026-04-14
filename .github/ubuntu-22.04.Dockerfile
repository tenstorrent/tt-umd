FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV OS_CODENAME=jammy

COPY docker_install_common.sh /docker_install_common.sh
RUN chmod +x /docker_install_common.sh && /docker_install_common.sh

COPY docker_install_cross.sh /docker_install_cross.sh
RUN chmod +x /docker_install_cross.sh && /docker_install_cross.sh

